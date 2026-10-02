# Testing

Run everything: `make test` (integration), `make unit-test` (router). Real outputs: `sample_output.txt`.

| # | Scenario | Expected | Automated check |
|---|---|---|---|
| U | Router unit test (RR order, skipping, RECOVERING, none healthy, LC) | all asserts pass | `tests/test_router.cpp` |
| 1 | All 3 servers up, 9 requests | 3 requests each (round robin) | `run_tests.sh` Test 1 |
| 2 | Kill Server 2 | UNHEALTHY logged, 0 requests to it, no failed requests | Test 2 |
| 3 | Restart Server 2 | RECOVERING -> "recovered", gets traffic again | Test 3 |
| 4 | 10 concurrent clients x 5 requests | 50 ok / 0 failed | Test 4 |
| 5 | SIGINT to balancer | exit 0, "shutting down" + statistics logged, port closed | Test 5 |
| 6 | Dead + malformed backend lines in config | bad lines skipped, dead server UNHEALTHY, system keeps serving | Test 6 |

Result on the development machine (Ubuntu 24.04): **19 passed, 0 failed** (see `sample_output.txt`).
Re-run it on your own machine and use your own output/screenshots in the report.

## Using Linux tools while testing
```bash
ps -o pid,nlwp,rss,cmd -C load_balancer,backend_server   # processes, thread count (nlwp), memory
top -H -p $(pgrep load_balancer)                         # per-thread CPU usage (q to quit)
htop -p $(pgrep -d, -f 'load_balancer|backend_server')   # interactive version
ss -ltnp | grep -E '8080|900[1-3]'                       # who listens on which port
ss -tn state established '( sport = :8080 )'             # live client connections to the LB
netstat -tlnp | grep 900                                 # older equivalent of ss
lsof -p $(pgrep load_balancer)                           # open fds: sockets, log file
ls /proc/$(pgrep load_balancer)/fd | wc -l               # fd count: must not grow (leak check)
kill -9 <backend-pid>                                    # crash a backend (failure test)
kill -USR1 <lb-pid>                                      # live statistics
kill -INT <lb-pid>                                       # graceful shutdown
ip addr show lo ; ping -c 2 127.0.0.1                    # loopback interface / reachability
strace -f -e trace=network -p $(pgrep load_balancer)     # watch socket system calls
tail -f logs/load_balancer.log                           # watch events live
```
What each shows: `ss`/`lsof` prove the balancer listens on 8080 and has sockets to backends; `ps nlwp` proves 8 workers + 1 health thread + 1 admin thread + main (=11 threads);
the fd count staying constant after many requests proves there are no descriptor leaks; `top -H` shows idle threads using 0 % CPU (blocked in `poll`/`cv.wait`).

## Optional debugging tools
```bash
valgrind --leak-check=full ./bin/backend_server 1 9001   # memory leaks (sudo apt install valgrind)
make clean && make CXXFLAGS="-std=c++17 -g -O1 -pthread -Iinclude -fsanitize=thread -MMD -MP" LDFLAGS="-pthread -fsanitize=thread"   # ThreadSanitizer build, then: make test  (any race is printed as a report)
gdb --args ./bin/load_balancer                           # debugger (bt, info threads)
```

## Common errors and fixes
| Problem | Cause / fix |
|---|---|
| `bind: Address already in use` | another instance running: `ss -ltnp \| grep 8080`, `kill` it, or use `-p` |
| All requests return `ERROR: no healthy backend` | backends not running or wrong ports in `config/servers.conf`; read the log |
| `Cannot open config file` | run from the project root or pass `-c path` |
| Server 2 not used right after restart | it must pass 2 probes (about 2 x interval) |
| `make: g++: command not found` | `sudo apt install build-essential` |

## ThreadSanitizer result
The project was also built with `-fsanitize=thread` (command above) and the full integration suite was run: 19 passed, 0 failed, and no ThreadSanitizer data-race reports were produced.

## GUI tests (`make gui-test`)
| # | Test | Where |
|---|---|---|
| G1-G12 | Controller test on real processes: Start -> 3 HEALTHY; 6 requests -> total 6, 2 per server; `/proc` RSS/threads valid; Stop Server 2 -> UNHEALTHY; requests still succeed; Start Server 2 -> HEALTHY; Health Check; log contains events; Stop -> everything gone | `tests/test_controller.cpp` |
| W1 | Real window on Xvfb: click Start, Send Request x3, row Stop on Server 2, Server Status tab, Stop; screenshots saved | `tests/run_gui_tests.sh`, `tests/xclick.cpp` |

Result on the development machine: **12 passed, 0 failed**; the original 19 integration checks and the router unit test still pass after adding the GUI.
Manual check with the command-line tool: `./bin/lbctl status` prints the same numbers the GUI shows.
