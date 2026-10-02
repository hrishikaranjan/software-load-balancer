# Stage 2 - Requirements, PRD and Planning

## Functional requirements
| ID | Requirement |
|---|---|
| FR1 | Backend server listens on a configurable port and answers with its server ID |
| FR2 | Backend logs each request and can simulate failure (SIGUSR1) |
| FR3 | Load balancer accepts TCP clients and forwards each request to one backend, returning the response |
| FR4 | Backends are read from `config/servers.conf` |
| FR5 | Round Robin selection skipping non-HEALTHY servers; pluggable algorithm interface |
| FR6 | Periodic health check; states HEALTHY / UNHEALTHY / RECOVERING |
| FR7 | Per-backend data: id, ip, port, status, active connections, total/ok/failed requests, last check |
| FR8 | Concurrent clients through threads, shared data protected by synchronisation |
| FR9 | SIGINT/SIGTERM graceful shutdown; final statistics |
| FR10 | Log file with INFO/WARNING/ERROR events |
| FR11 | Test client: single, repeated and concurrent requests |
| FR12 | Retry on another backend when one fails mid-request |

## Non-functional requirements
Reliability (no crash on bad config, dead backend, closed client); thread safety (no data races); no fd/memory leaks; portability across Linux distros;
clean modular code; readable logs; startup < 2 s; builds with only `g++` and `make`; **C/C++ only**.

## Hardware requirements
Any x86-64 or ARM64 PC/VM: 1 CPU core (2+ recommended to see real concurrency), 512 MB RAM, ~50 MB disk. Network: loopback only.

## Software requirements
Linux (Ubuntu 20.04+/Debian/Fedora, native or VM/WSL2), g++ >= 7 (C++17), GNU make, Bash, Git. Optional: htop, net-tools, lsof, iproute2, strace, valgrind, gdb.

## Linux environment requirements
Free ports 8080 and 9001-9003; writable project directory (for `logs/`); ability to run several processes (4-5 terminals or `tests/run_tests.sh`).

## User / system requirements
User can start servers, start the balancer, send requests, kill/restart servers, read logs, and stop with Ctrl+C. System must keep serving while any one backend is down.

## Timeline (6-week plan, adjust to your calendar)
| Week | Work |
|---|---|
| 1 | Stage 1-2 documents, repository, Makefile, project skeleton |
| 2 | Stage 3 design/UML; backend server + client; first socket test |
| 3 | Load balancer prototype with round robin (Stage 4) |
| 4 | Health checker, threads/thread pool, failure/recovery |
| 5 | Logging, statistics, signals, tests, bug fixing (Stage 5) |
| 6 | Documentation, report, demo rehearsal, GitHub cleanup (Stage 6) |

## Development plan
Incremental: prototype first (single-threaded), then add health checks, threads, logging, statistics, signals; test after each step; one Git commit per feature.

## Risks and mitigation
| Risk | Mitigation |
|---|---|
| Data races / deadlocks | one mutex per shared structure, RAII guards, fixed lock order, ThreadSanitizer run |
| Blocking calls hang threads | non-blocking connect + `poll()` timeouts, socket I/O timeouts |
| Port conflicts during demo | configurable ports, pre-check in tests |
| SIGPIPE kills process | ignore SIGPIPE, `MSG_NOSIGNAL` |
| Flapping servers | RECOVERING state needs 2 good probes |
| Running out of time | prototype stage always keeps a working system |
| Resource leaks | close on every path, `/proc/<pid>/fd` check, valgrind |
