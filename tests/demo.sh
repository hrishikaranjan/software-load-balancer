#!/usr/bin/env bash
# Presentation demo: load balancing -> failure -> detection -> recovery -> statistics.
# Usage: bash tests/demo.sh          (add "pause" as $1 to wait for ENTER between steps)
cd "$(dirname "$0")/.."
BIN=bin; PAUSE=${1:-}
step() { echo; echo "################ $1"; [ "$PAUSE" = pause ] && read -r -p "(press ENTER) " _; }
mkdir -p logs/demo
for n in 1 2 3; do $BIN/backend_server $n $((9000+n)) > logs/demo/backend$n.out 2>&1 & eval "B$n=$!"; done
$BIN/load_balancer -i 1000 -l logs/demo/load_balancer.log > logs/demo/lb.out 2>&1 & LB=$!
trap 'kill $LB $B1 $B2 $B3 2>/dev/null; wait 2>/dev/null' EXIT
sleep 1.5

step "1. Processes and listening sockets (ps / lsof or ss)"
ps -o pid,nlwp,rss,cmd -p $LB,$B1,$B2,$B3          # nlwp = number of threads
if command -v ss >/dev/null; then ss -ltnp | grep -E ':(8080|9001|9002|9003)\b'; fi
if command -v lsof >/dev/null; then lsof -a -p $LB -i -P -n; fi

step "2. Round robin with all servers up"
$BIN/client -n 6 -m demo

step "3. Crash Server 2 (kill -9) and wait for detection"
kill -9 $B2; wait $B2 2>/dev/null; sleep 2.5
grep -E "Server 2" logs/demo/load_balancer.log | tail -3
$BIN/client -n 6 -m demo

step "4. Restart Server 2 -> RECOVERING -> HEALTHY"
$BIN/backend_server 2 9002 > logs/demo/backend2.out 2>&1 & B2=$!
sleep 3.5
grep -E "Server 2" logs/demo/load_balancer.log | tail -3
$BIN/client -n 6 -m demo

step "5. Simulated failure of Server 3 using SIGUSR1 (port closed, process alive)"
kill -USR1 $B3; sleep 2.5
$BIN/client -n 4 -m demo
kill -USR1 $B3; sleep 3.5
$BIN/client -n 3 -m demo

step "6. Live statistics with SIGUSR1, then graceful shutdown with SIGINT"
kill -USR1 $LB; sleep 1; tail -16 logs/demo/load_balancer.log
kill -INT $LB; wait $LB; echo "load_balancer exit code: $?"
tail -22 logs/demo/load_balancer.log
