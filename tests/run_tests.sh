#!/usr/bin/env bash
# Integration tests for the Software-Defined Load Balancer (Tests 1-6 from the project spec).
# Only uses Linux tools (bash, kill, ss, grep, awk) to drive the C++ binaries.
# Run from anywhere:  bash tests/run_tests.sh      (or: make test)
set -u
cd "$(dirname "$0")/.."
BIN=bin
OUT=logs/tests
LB_PORT=8080
rm -rf "$OUT"; mkdir -p "$OUT"
PASS=0; FAIL=0
LB_PID=""

start_backend() {   # $1 = server number; port = 9000 + number
    $BIN/backend_server "$1" $((9000 + $1)) >> "$OUT/backend$1.out" 2>&1 &
    echo $! > "$OUT/backend$1.pid"
}
stop_backend() {
    local pid; pid=$(cat "$OUT/backend$1.pid" 2>/dev/null) || return
    kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
}
start_lb() {        # $1 = config file
    $BIN/load_balancer -p $LB_PORT -c "$1" -i 500 -l "$OUT/load_balancer.log" > "$OUT/lb.out" 2>&1 &
    LB_PID=$!
}
cleanup() {
    [ -n "$LB_PID" ] && kill "$LB_PID" 2>/dev/null
    for n in 1 2 3; do stop_backend $n; done
    wait 2>/dev/null
}
trap cleanup EXIT

port_open() { (exec 3<>/dev/tcp/127.0.0.1/"$1") 2>/dev/null; }
check() {           # check "description" <condition 0/1>
    if [ "$2" -eq 1 ]; then echo "  PASS: $1"; PASS=$((PASS+1)); else echo "  FAIL: $1"; FAIL=$((FAIL+1)); fi
}
count_of() {        # count_of <server-number> <client-output>
    local c; c=$(echo "$2" | awk -v s="$1" '$1=="Backend" && $3==s {print $5}'); echo "${c:-0}"
}
eq() { [ "$1" -eq "$2" ] && echo 1 || echo 0; }

if port_open 8080 || port_open 9001 || port_open 9002 || port_open 9003; then
    echo "Ports 8080/9001-9003 are already in use. Stop other instances first."; exit 2
fi
[ -x $BIN/load_balancer ] || { echo "Run 'make' first"; exit 2; }

echo "=== Test 1: all servers up -> round robin ==="
for n in 1 2 3; do start_backend $n; done
start_lb config/servers.conf
sleep 1.5
out=$($BIN/client -n 9 -m ping -q)
echo "$out" | sed 's/^/    /'
check "Server 1 got 3 requests" "$(eq "$(count_of 1 "$out")" 3)"
check "Server 2 got 3 requests" "$(eq "$(count_of 2 "$out")" 3)"
check "Server 3 got 3 requests" "$(eq "$(count_of 3 "$out")" 3)"

echo "=== Test 2: stop Server 2 -> detected UNHEALTHY, gets no traffic ==="
stop_backend 2
sleep 2
check "log shows Server 2 UNHEALTHY" "$(grep -q 'Backend Server 2 .*is UNHEALTHY' $OUT/load_balancer.log && echo 1 || echo 0)"
out=$($BIN/client -n 6 -m ping -q)
echo "$out" | sed 's/^/    /'
check "Server 2 got 0 requests" "$(eq "$(count_of 2 "$out")" 0)"
check "Server 1 got 3 requests" "$(eq "$(count_of 1 "$out")" 3)"
check "Server 3 got 3 requests" "$(eq "$(count_of 3 "$out")" 3)"
check "no failed requests" "$(echo "$out" | grep -q ' 0 failed' && echo 1 || echo 0)"

echo "=== Test 3: restart Server 2 -> RECOVERED and used again ==="
start_backend 2
sleep 3
check "log shows Server 2 recovered" "$(grep -q 'Backend Server 2 recovered' $OUT/load_balancer.log && echo 1 || echo 0)"
out=$($BIN/client -n 9 -m ping -q)
echo "$out" | sed 's/^/    /'
check "Server 2 got 3 requests again" "$(eq "$(count_of 2 "$out")" 3)"

echo "=== Test 4: 10 concurrent clients x 5 requests ==="
out=$($BIN/client -c 10 -n 5 -m load -q)
echo "$out" | sed 's/^/    /'
check "50 sent, 50 ok, 0 failed" "$(echo "$out" | grep -q '50 sent, 50 ok, 0 failed' && echo 1 || echo 0)"

echo "=== Test 5: SIGINT -> graceful shutdown ==="
kill -INT "$LB_PID"
wait "$LB_PID"; rc=$?
LB_PID=""
check "exit code 0" "$(eq $rc 0)"
check "log has shutdown message" "$(grep -q 'Load balancer shutting down' $OUT/load_balancer.log && echo 1 || echo 0)"
check "log has final statistics" "$(grep -q 'LOAD BALANCER STATISTICS' $OUT/load_balancer.log && echo 1 || echo 0)"
check "port $LB_PORT is closed" "$(port_open $LB_PORT && echo 0 || echo 1)"
grep -A12 'LOAD BALANCER STATISTICS' $OUT/load_balancer.log | sed 's/^/    /'

echo "=== Test 6: invalid / dead backend in config ==="
for n in 2 3; do stop_backend $n; done
: > $OUT/load_balancer.log
start_lb tests/invalid_backend.conf
sleep 1.5
check "load balancer still running" "$(kill -0 "$LB_PID" 2>/dev/null && echo 1 || echo 0)"
out=$($BIN/client -n 4 -m ping -q)
echo "$out" | sed 's/^/    /'
check "all 4 requests served by Server 1" "$(eq "$(count_of 1 "$out")" 4)"
check "dead backend logged as UNHEALTHY" "$(grep -q 'is UNHEALTHY' $OUT/load_balancer.log && echo 1 || echo 0)"
check "malformed config lines logged and skipped" "$(grep -q 'invalid, skipped' $OUT/load_balancer.log && echo 1 || echo 0)"

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
