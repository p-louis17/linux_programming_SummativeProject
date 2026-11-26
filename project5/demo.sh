#!/bin/bash
#
# demo.sh  (Stage 7: functional demonstration)
#
# Usage: ./demo.sh
#
# Runs the normal ./server on port 9091 and drives real ./client programs
# through every scenario in Part D of the assignment that race_test.sh does
# not cover. Everything printed here is also saved under demo_logs/functional.
#
# Needs ./server, ./client and students.txt in the current folder.

PORT=9091
OUT=demo_logs/functional

rm -rf "$OUT"
mkdir -p "$OUT"

# Prints a banner for each scenario.
section() {
    echo
    echo "=============================================================="
    echo "$1"
    echo "=============================================================="
}

# run_client <logfile> <line> <line> ...
# Runs one client whose keyboard input is the given lines, one per line.
run_client() {
    local log=$1
    shift
    printf '%s\n' "$@" | ./client 127.0.0.1 "$PORT" > "$log" 2>&1
}

# Prints the server's answers to registration requests found in a client log.
# The reply can share a line with the menu prompt, so match it anywhere.
results_of() {
    grep -h -o -E '(Success|Event full|Duplicate registration|Event unavailable): .*' "$1"
}

# Start the server.
./server "$PORT" students.txt > "$OUT/server.log" 2>&1 &
SERVER_PID=$!
sleep 0.5

# ---------------------------------------------------------------- 1
section "1. Unsuccessful authentication (unknown ID S9999)"
run_client "$OUT/01_failed_auth.log" S9999
cat "$OUT/01_failed_auth.log"

# ---------------------------------------------------------------- 2
section "2. Successful authentication, event list, registration, duplicate, unknown event, graceful quit (S1001)"
run_client "$OUT/02_login_register.log" S1001 101 101 999 q
cat "$OUT/02_login_register.log"

# ---------------------------------------------------------------- 3
section "3. Registering for an event that is already full (Cybersecurity Seminar, capacity 4)"
for id in S1002 S1003 S1004 S1005 S1006; do
    run_client "$OUT/03_full_$id.log" "$id" 102 q
    echo "$id: $(results_of "$OUT/03_full_$id.log")"
done

# ---------------------------------------------------------------- 4
section "4. Five simultaneous clients, all registering for the Research Conference (capacity 6)"
PIDS=()
for id in S1001 S1002 S1003 S1004 S1005; do
    (
        echo "$id"
        while [ ! -e "$OUT/go" ]; do sleep 0.005; done
        echo 103
        echo q
    ) | ./client 127.0.0.1 "$PORT" > "$OUT/04_simul_$id.log" 2>&1 &
    PIDS+=($!)
done
sleep 2                                   # all five connected and authenticated
touch "$OUT/go"                           # release them together
wait "${PIDS[@]}"

for id in S1001 S1002 S1003 S1004 S1005; do
    echo "$id: $(results_of "$OUT/04_simul_$id.log")"
done
peak=$(grep -o 'total [0-9]*' "$OUT/server.log" | awk '{print $2}' | sort -n | tail -1)
echo
echo "Highest number of clients connected at the same time (from the server log): $peak"
if [ "$peak" -ge 5 ]; then
    echo "RESULT: PASS, at least five simultaneous connections"
else
    echo "RESULT: FAIL, fewer than five simultaneous connections"
fi

# ---------------------------------------------------------------- 5
section "5. Unexpected client disconnection (S1008 killed with kill -9, no QUIT sent)"
before=$(wc -l < "$OUT/server.log")
( echo S1008; sleep 5 ) | ./client 127.0.0.1 "$PORT" > "$OUT/05_dropped_client.log" 2>&1 &
DROP_PID=$!
sleep 1
echo "Client S1008 is connected. Killing it abruptly now."
kill -9 "$DROP_PID"
wait "$DROP_PID" 2>/dev/null
sleep 0.5
echo
echo "Server log lines written since then:"
tail -n +$((before + 1)) "$OUT/server.log"

echo
echo "The server is still alive: a new client connects and sees the event table."
run_client "$OUT/05_after_drop.log" S1001 q
grep -h -E '^(ID |10[123] )' "$OUT/05_after_drop.log"

# Stop the server.
kill "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null

echo
echo "Logs saved in $OUT/"
