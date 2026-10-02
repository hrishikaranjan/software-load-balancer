# Stage 5 - Testing, Integration and Improvement

Improvements added on top of the prototype and where to see them:

| Improvement | File | How to observe |
|---|---|---|
| Multithreading (thread pool) | `thread_pool.cpp` | `ps -o nlwp -C load_balancer` shows 11 threads; `client -c 10 -n 5` |
| Health monitoring + failure + recovery | `health_checker.cpp`, `backend_pool.cpp` | kill/restart a backend, watch `tail -f logs/load_balancer.log` |
| Logging | `logger.cpp` | `logs/load_balancer.log`, `logs/backend_N.log` |
| Statistics | `statistics.cpp` | printed at shutdown; `kill -USR1 <pid>` for live view |
| Signal handling | `signal_handler.cpp` | Ctrl+C, `kill -TERM`, `kill -USR1` |
| Error handling | `net_utils.cpp`, `load_balancer.cpp` | dead backend, bad config, client disconnects, SIGPIPE |
| Retry on failure | `forward_request()` | kill a backend and immediately send requests: still 0 failed |

Test cases, commands and results: [`../testing.md`](../testing.md) and [`../sample_output.txt`](../sample_output.txt)
(19 automated checks passed; ThreadSanitizer run clean).
Re-run on your machine with `make test` and keep your own output for the report.

Bugs/pitfalls worth mentioning in the report (all handled in the code): blocking `connect()` to a dead host, SIGPIPE on closed sockets, signal delivered to a non-main thread (solved with `poll()` timeout), double `join` on shutdown (idempotent `shutdown()`), lock held during network I/O (health probes run outside the mutex).
