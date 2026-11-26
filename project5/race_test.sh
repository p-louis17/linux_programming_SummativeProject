#!/bin/bash
#
# race_test.sh  (Stage 7: simultaneous registration test)
#
# Usage: ./race_test.sh [server_binary] [racers] [prefill]
#   server_binary  which server to test (default ./server_demo)
#   racers         number of clients registering at the same moment (default 8)
#   prefill        seats filled one by one before the race (default 0)
#
# Examples:
#   ./race_test.sh ./server_broken 8 0   8 clients race for 4 seats, no mutex
#   ./race_test.sh ./server_demo   8 0   same race, with the mutex
#   ./race_test.sh ./server_demo   2 3   3 seats filled, 2 clients race for the last one
#
# Needs the binaries ./client and the server binary, and students.txt,
# all in the current folder. Logs are written to demo_logs/.

SERVER=${1:-./server_demo}
RACERS=${2:-8}
PREFILL=${3:-0}

PORT=9090            # not 8080, so a server left running there cannot clash
EVENT=102            # Cybersecurity Seminar
CAPACITY=4           # its capacity in server.c
IDS=(S1001 S1002 S1003 S1004 S1005 S1006 S1007 S1008)

if [ $((RACERS + PREFILL)) -gt ${#IDS[@]} ]; then
    echo "racers + prefill cannot be more than ${#IDS[@]} (one student ID each)"
    exit 1
fi

LABEL=$(basename "$SERVER")
OUT="demo_logs/race_${LABEL}_r${RACERS}_p${PREFILL}"
rm -rf "$OUT"
mkdir -p "$OUT"

echo "=== Race test: $LABEL, $RACERS racers, $PREFILL seats pre-filled, event $EVENT capacity $CAPACITY ==="

# 1. Start the server in the background, its output goes to the log file.
"$SERVER" "$PORT" students.txt > "$OUT/server.log" 2>&1 &
SERVER_PID=$!
sleep 0.5

# 2. Pre-fill seats one student at a time (no race here).
for ((i = 0; i < PREFILL; i++)); do
    id=${IDS[$i]}
    printf '%s\n%s\nq\n' "$id" "$EVENT" | ./client 127.0.0.1 "$PORT" > "$OUT/prefill_$id.log" 2>&1
done

# 3. Launch the racers. Each client sends its ID, then waits until the file
#    "go" exists, then sends the registration, lists events and quits.
PIDS=()
for ((i = PREFILL; i < PREFILL + RACERS; i++)); do
    id=${IDS[$i]}
    (
        echo "$id"
        while [ ! -e "$OUT/go" ]; do sleep 0.005; done
        echo "$EVENT"
        echo "l"
        echo "q"
    ) | ./client 127.0.0.1 "$PORT" > "$OUT/client_$id.log" 2>&1 &
    PIDS+=($!)
done

# 4. Give every client time to connect and authenticate, then open the gate.
sleep 2
echo "All $RACERS clients are connected and authenticated. Releasing the barrier now."
touch "$OUT/go"

# 5. Wait for all racers to finish.
wait "${PIDS[@]}"

# 6. Audit: one more client asks the server for its final event table,
#    after every racer has finished. This is the authoritative result.
printf 'S1001\nl\nq\n' | ./client 127.0.0.1 "$PORT" > "$OUT/audit.log" 2>&1

# 7. Stop the server.
kill "$SERVER_PID"
wait "$SERVER_PID" 2>/dev/null

# 8. Evidence.
echo
echo "--- Server log: registration decisions ---"
grep '\[register\]' "$OUT/server.log"

echo
echo "--- What each racing client was told ---"
for ((i = PREFILL; i < PREFILL + RACERS; i++)); do
    id=${IDS[$i]}
    # The reply can share a line with the menu prompt, so match it anywhere.
    result=$(grep -h -o -E '(Success|Event full|Duplicate registration|Event unavailable): .*' \
             "$OUT/client_$id.log" | head -1)
    echo "$id: $result"
done

ok=$(grep -c "registered for $EVENT " "$OUT/server.log")
full=$(grep -c "rejected, $EVENT " "$OUT/server.log")

echo
echo "--- Summary ---"
echo "Registrations accepted for event $EVENT: $ok (capacity $CAPACITY)"
echo "Registrations rejected as full:          $full"
echo "Final event table from the audit client:"
grep -h "^$EVENT " "$OUT/audit.log" | tail -1

if [ "$ok" -le "$CAPACITY" ]; then
    echo "RESULT: PASS, capacity was never exceeded"
else
    echo "RESULT: FAIL, event was overbooked ($ok accepted for $CAPACITY places)"
fi