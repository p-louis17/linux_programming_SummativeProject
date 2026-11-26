#!/bin/bash
# run_tests.sh: build sensor_stats and run it against several test inputs.
# Each test gets its own temporary folder because the program always reads
# a file named sensor_readings.txt from the current directory.
# Usage: ./run_tests.sh | tee execution_evidence.txt

set -e
cd "$(dirname "$0")"
nasm -f elf64 sensor_stats.asm -o sensor_stats.o
ld sensor_stats.o -o sensor_stats
BIN="$PWD/sensor_stats"

run_case() {                      # $1 = title, $2 = printf format of the file
    local dir
    dir=$(mktemp -d)
    echo "=================================================="
    echo "TEST: $1"
    echo "--- input (cat -A shows \$ at line ends and ^M for CR) ---"
    printf "$2" > "$dir/sensor_readings.txt"
    cat -A "$dir/sensor_readings.txt"; echo
    echo "--- output ---"
    (cd "$dir" && "$BIN"; echo "exit status: $?") || true
    rm -rf "$dir"
    echo
}

run_case "Assignment example (valid and invalid mixed)" '25\n30\ninvalid\n18\n42\n-5\n'
run_case "All valid"                                    '10\n20\n30\n'
run_case "Empty lines and Windows CRLF endings"         '10\r\n\r\n100\r\n101\r\nabc\r\n0\r\n'
run_case "Range boundaries and leading zeros"           '0\n100\n101\n-1\n007\n99\n'
run_case "Last line has no trailing newline"            '5\n7'
run_case "No valid measurements"                        'x\n\n-1\n'
run_case "Empty file"                                   ''

echo "=================================================="
echo "TEST: Missing input file"
dir=$(mktemp -d)
(cd "$dir" && "$BIN"; echo "exit status: $?") || true
rm -rf "$dir"