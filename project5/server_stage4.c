#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#define DEFAULT_PORT      8080
#define DEFAULT_STUDENTS  "students.txt"
#define MAX_CLIENTS       20
#define MAX_STUDENTS      100
#define STUDENT_ID_SIZE   32
#define LINE_SIZE         256
#define MAX_EVENTS        3
#define MAX_CAPACITY      10
#define EVENT_NAME_SIZE   48

/* One slot in the connected clients list. */
typedef struct {
    int  fd;                           /* socket descriptor of this client */
    char ip[INET_ADDRSTRLEN];          /* client address as text */
    int  port;                         /* client port */
    int  active;                       /* 1 if the slot is in use */
    char student[STUDENT_ID_SIZE];     /* student ID, empty until authenticated */
} client_t;

/* One university event. */
typedef struct {
    int  id;                                        /* event ID, e.g. 101 */
    char name[EVENT_NAME_SIZE];                     /* event name */
    int  capacity;                                  /* maximum registrations */
    int  count;                                     /* current registrations */
    char registered[MAX_CAPACITY][STUDENT_ID_SIZE]; /* who is registered */
} event_t;

/* Results of a registration attempt. */
enum { RESULT_OK, RESULT_DUPLICATE, RESULT_FULL, RESULT_INVALID };

/* Shared data 1: the clients list. Every access must hold clients_mutex. */
static client_t        clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Shared data 2: the event table. Every access to count and registered[]
 * must hold events_mutex. id, name and capacity never change after startup. */
static event_t events[MAX_EVENTS] = {
    { 101, "AI Workshop",          5, 0, {{0}} },
    { 102, "Cybersecurity Seminar", 4, 0, {{0}} },
    { 103, "Research Conference",  6, 0, {{0}} }
};
static pthread_mutex_t events_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Registered students. Filled once in main before any thread exists,
 * and only read afterwards, so no mutex is needed. */
static char students[MAX_STUDENTS][STUDENT_ID_SIZE];
static int  student_count = 0;

/* Reads the student file, one ID per line. Returns the number loaded,
 * or -1 if the file cannot be opened. */
static int load_students(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror(path);
        return -1;
    }

    char line[STUDENT_ID_SIZE + 8];
    while (student_count < MAX_STUDENTS && fgets(line, sizeof(line), fp) != NULL) {
        /* strip trailing newline, carriage return and spaces */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                           line[len - 1] == ' '))
            line[--len] = '\0';

        if (len == 0 || len >= STUDENT_ID_SIZE)
            continue;                  /* skip blank or oversized lines */

        strcpy(students[student_count++], line);
    }

    fclose(fp);
    return student_count;
}

/* Returns 1 if the ID is in the registered list, 0 otherwise. */
static int is_registered(const char *id)
{
    for (int i = 0; i < student_count; i++) {
        if (strcmp(students[i], id) == 0)
            return 1;
    }
    return 0;
}

/* Prints the list. Caller must already hold clients_mutex. */
static void print_clients_locked(void)
{
    int count = 0;
    printf("[server] connected clients:");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            printf(" %s:%d(%s)", clients[i].ip, clients[i].port,
                   clients[i].student[0] ? clients[i].student : "unauthenticated");
            count++;
        }
    }
    printf(" (total %d)\n", count);
}

/* Adds a client to the list. Returns its slot index, or -1 if the list is full. */
static int add_client(int fd, const struct sockaddr_in *addr)
{
    int slot = -1;

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].fd         = fd;
            clients[i].port       = ntohs(addr->sin_port);
            clients[i].active     = 1;
            clients[i].student[0] = '\0';
            inet_ntop(AF_INET, &addr->sin_addr, clients[i].ip, sizeof(clients[i].ip));
            slot = i;
            break;
        }
    }
    if (slot >= 0)
        print_clients_locked();
    pthread_mutex_unlock(&clients_mutex);

    return slot;
}

/* Records which student owns a slot after successful authentication. */
static void set_client_student(int slot, const char *id)
{
    pthread_mutex_lock(&clients_mutex);
    snprintf(clients[slot].student, sizeof(clients[slot].student), "%s", id);
    print_clients_locked();
    pthread_mutex_unlock(&clients_mutex);
}

/* Removes a client from the list. */
static void remove_client(int slot)
{
    pthread_mutex_lock(&clients_mutex);
    clients[slot].active = 0;
    clients[slot].student[0] = '\0';
    print_clients_locked();
    pthread_mutex_unlock(&clients_mutex);
}

/* Sends text followed by a newline. Returns 0 on success, -1 on failure.
 * MSG_NOSIGNAL stops a send to a closed socket from raising SIGPIPE. */
static int send_line(int fd, const char *text)
{
    char buf[LINE_SIZE + 2];
    int  len = snprintf(buf, sizeof(buf), "%s\n", text);
    int  sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        sent += n;
    }
    return 0;
}

/* Reads one line (up to the newline) into buf, without the newline.
 * Returns the line length (0 means an empty line), or -1 if the client
 * disconnected or a socket error happened.
 * TCP is a byte stream, so we read one byte at a time until '\n'. */
static int recv_line(int fd, char *buf, size_t size)
{
    size_t len = 0;

    while (len < size - 1) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0)
            return -1;                  /* client closed the connection */
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;                  /* socket error */
        }
        if (c == '\n')
            break;
        if (c != '\r')                  /* ignore carriage returns from telnet/nc */
            buf[len++] = c;
    }
    buf[len] = '\0';
    return (int)len;
}

/* Sends every event as "EVENT id|name|capacity|registered", then "END".
 * The lines are built while holding the lock, but sent after releasing it,
 * so a slow client can never block other students' registrations. */
static int send_event_list(int fd)
{
    char lines[MAX_EVENTS][LINE_SIZE];

    pthread_mutex_lock(&events_mutex);
    for (int i = 0; i < MAX_EVENTS; i++) {
        snprintf(lines[i], sizeof(lines[i]), "EVENT %d|%s|%d|%d",
                 events[i].id, events[i].name, events[i].capacity, events[i].count);
    }
    pthread_mutex_unlock(&events_mutex);

    for (int i = 0; i < MAX_EVENTS; i++) {
        if (send_line(fd, lines[i]) < 0)
            return -1;
    }
    return send_line(fd, "END");
}

/* Finds an event by ID. Returns NULL if there is no such event.
 * Safe without the lock, because id never changes after startup. */
static event_t *find_event(int event_id)
{
    for (int i = 0; i < MAX_EVENTS; i++) {
        if (events[i].id == event_id)
            return &events[i];
    }
    return NULL;
}

/* Tries to register a student for an event.
 * The whole sequence (find, duplicate check, capacity check, insert) runs
 * inside ONE critical section. That is what guarantees two students can
 * never both take the last remaining place.
 * name_out receives the event name, count_out the count after the attempt. */
static int register_for_event(const char *student, int event_id,
                              char *name_out, size_t name_size,
                              int *count_out, int *capacity_out)
{
    int result;

    event_t *ev = find_event(event_id);
    if (ev == NULL) {
        name_out[0] = '\0';
        *count_out = 0;
        *capacity_out = 0;
        return RESULT_INVALID;
    }

    pthread_mutex_lock(&events_mutex);

    int already = 0;
    for (int i = 0; i < ev->count; i++) {
        if (strcmp(ev->registered[i], student) == 0) {
            already = 1;
            break;
        }
    }

    if (already) {
        result = RESULT_DUPLICATE;
    } else if (ev->count >= ev->capacity) {
        result = RESULT_FULL;
    } else {
        snprintf(ev->registered[ev->count], STUDENT_ID_SIZE, "%s", student);
        ev->count++;
        result = RESULT_OK;
    }

    snprintf(name_out, name_size, "%s", ev->name);
    *count_out    = ev->count;
    *capacity_out = ev->capacity;

    pthread_mutex_unlock(&events_mutex);
    return result;
}

/* Handles "REGISTER <id>" for an authenticated student. Returns 0 on
 * success, -1 if the reply could not be sent. */
static int handle_register(int fd, const char *student, const char *args)
{
    int  event_id;
    char name[EVENT_NAME_SIZE];
    char reply[LINE_SIZE];
    int  count, capacity;

    if (sscanf(args, "%d", &event_id) != 1)
        return send_line(fd, "REG_INVALID Event ID must be a number");

    int result = register_for_event(student, event_id, name, sizeof(name),
                                    &count, &capacity);

    switch (result) {
    case RESULT_OK:
        snprintf(reply, sizeof(reply), "REG_OK Registered for %s (%d/%d places taken)",
                 name, count, capacity);
        printf("[register] %s registered for %d %s (%d/%d)\n",
               student, event_id, name, count, capacity);
        break;
    case RESULT_DUPLICATE:
        snprintf(reply, sizeof(reply), "REG_DUPLICATE You are already registered for %s",
                 name);
        printf("[register] %s duplicate attempt for %d %s\n", student, event_id, name);
        break;
    case RESULT_FULL:
        snprintf(reply, sizeof(reply), "REG_FULL %s is full (%d/%d)",
                 name, count, capacity);
        printf("[register] %s rejected, %d %s is full\n", student, event_id, name);
        break;
    default:
        snprintf(reply, sizeof(reply), "REG_INVALID No event with ID %d", event_id);
        printf("[register] %s asked for unknown event %d\n", student, event_id);
        break;
    }

    return send_line(fd, reply);
}

/* Thread function: handles one client from connect to disconnect. */
static void *client_thread(void *arg)
{
    int slot = *(int *)arg;
    free(arg);

    int  fd = clients[slot].fd;
    char line[LINE_SIZE];
    char id[STUDENT_ID_SIZE];

    printf("[thread] client %s:%d connected\n", clients[slot].ip, clients[slot].port);

    /* Authentication: the very first line must be a registered student ID.
     * Nothing else is reachable until this check passes. */
    int n = recv_line(fd, id, sizeof(id));
    if (n < 0)
        goto done;                      /* left before sending an ID */

    if (!is_registered(id)) {
        printf("[auth] %s:%d rejected, unknown ID '%s'\n",
               clients[slot].ip, clients[slot].port, id);
        send_line(fd, "AUTH_FAIL Unknown student ID");
        goto done;
    }

    set_client_student(slot, id);
    printf("[auth] %s:%d authenticated as %s\n",
           clients[slot].ip, clients[slot].port, id);
    if (send_line(fd, "AUTH_OK") < 0)
        goto done;

    /* Authenticated: send the event list straight away. */
    if (send_event_list(fd) < 0)
        goto done;

    /* Command loop. recv_line returns -1 when the client disconnects,
     * which ends the loop and falls through to the cleanup below. */
    for (;;) {
        n = recv_line(fd, line, sizeof(line));
        if (n < 0)
            break;

        if (strcmp(line, "LIST") == 0) {
            if (send_event_list(fd) < 0)
                break;
        } else if (strncmp(line, "REGISTER ", 9) == 0) {
            if (handle_register(fd, id, line + 9) < 0)
                break;
        } else if (strcmp(line, "QUIT") == 0) {
            send_line(fd, "BYE");
            break;
        } else {
            if (send_line(fd, "ERROR Unknown command") < 0)
                break;
        }
    }

done:
    printf("[thread] client %s:%d disconnected\n", clients[slot].ip, clients[slot].port);
    close(fd);
    remove_client(slot);
    return NULL;
}

int main(int argc, char *argv[])
{
    int         port          = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    const char *students_file = (argc > 2) ? argv[2] : DEFAULT_STUDENTS;

    /* A write to a closed socket must not kill the server. */
    signal(SIGPIPE, SIG_IGN);

    /* Load the registered students before accepting anyone. */
    if (load_students(students_file) <= 0) {
        fprintf(stderr, "No registered students loaded from %s\n", students_file);
        return 1;
    }
    printf("[server] loaded %d registered students from %s\n",
           student_count, students_file);

    /* 1. Create the TCP socket. */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    /* Allow quick restarts without "Address already in use". */
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    /* 2. Bind to the port on all interfaces. */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    /* 3. Listen. The backlog of 10 queues clients that arrive together. */
    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("[server] listening on port %d\n", port);

    /* 4. Accept loop: one thread per client. */
    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        int slot = add_client(fd, &client_addr);
        if (slot < 0) {
            send_line(fd, "SERVER_FULL");
            close(fd);
            continue;
        }

        /* Pass the slot to the thread through the heap so each thread
         * gets its own copy of the value. */
        int *arg = malloc(sizeof(int));
        *arg = slot;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, arg) != 0) {
            perror("pthread_create");
            free(arg);
            close(fd);
            remove_client(slot);
            continue;
        }
        pthread_detach(tid);            /* no join needed, thread cleans itself up */
    }

    close(listen_fd);
    return 0;
}