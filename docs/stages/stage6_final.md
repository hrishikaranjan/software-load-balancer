# Stage 6 - Final Implementation and Presentation

## Deliverables checklist
- [x] Final source code (`src/`, `include/`) - [x] README - [x] Architecture/UML (`docs/`) - [x] Test results (`docs/sample_output.txt`)
- [ ] Your own screenshots of the terminals (take them while running `tests/demo.sh pause` - not included, only you can capture them)
- [ ] GitHub repository (create it yourself; nothing has been pushed for you)
- [ ] Final report (outline below)

## Git / GitHub
```bash
git init && git add . && git commit -m "Initial project structure"
git branch -M main
git remote add origin https://github.com/<your-user>/software-load-balancer.git
git push -u origin main          # you run this yourself
```
Suggested commit sequence (commit as you work or split the files accordingly):
```
Initial project structure
Implement TCP backend server
Implement test client
Implement load balancer
Add round robin routing
Add health checking
Add multithreaded client handling (thread pool)
Add logging
Add failure recovery (RECOVERING state, retry)
Add statistics
Add signal handling
Add test cases
Finalize documentation
```

## Final report outline
1 Introduction (Stage 1) - 2 Requirements and plan (Stage 2) - 3 Design and UML (Stage 3) - 4 Implementation by module - 5 System-programming concepts (table in README section 16 and `design.md` section 9) -
6 Testing and results - 7 Limitations - 8 Future enhancements - 9 Conclusion - 10 References (man pages: `socket(2) bind(2) listen(2) accept(2) connect(2) poll(2) sigaction(2) pthreads(7) tcp(7)`; Stevens, *UNIX Network Programming*; Kerrisk, *The Linux Programming Interface*).

## Limitations
IPv4 only; one request per TCP connection with a simple line protocol (not HTTP); no TLS; no connection reuse to backends; sequential health probes (slow with hundreds of backends);
in-memory statistics; fixed recovery threshold; no config reload.

## Future enhancements
Weighted RR, IP-hash/sticky sessions, `epoll` event loop, HTTP awareness, SIGHUP config reload, IPv6, TLS, connection pooling, circuit breaker, admin UNIX socket, Prometheus-style metrics endpoint written in C++.

## Presentation / demo script (about 8 minutes)
1. **Explain** (1 min): problem + architecture diagram from `docs/architecture.md`.
2. **Build** (30 s): `make` - mention flags `-std=c++17 -Wall -pthread`.
3. **Start** (1 min): 3 backends + balancer (or `bash tests/demo.sh pause`). Show `ps -o pid,nlwp,cmd` and `ss -ltnp` (processes, threads, listening sockets) and `tail -f logs/load_balancer.log`.
4. **Load balancing** (1 min): `./bin/client -n 9 -m demo` -> 1,2,3 rotation.
5. **Failure** (1.5 min): `kill -9` Server 2 -> show `[WARNING] ... is UNHEALTHY` in the log -> `./bin/client -n 6` only 1 and 3.
6. **Recovery** (1 min): restart Server 2 -> `RECOVERING` -> `recovered` -> traffic includes Server 2 again.
7. **Concurrency** (30 s): `./bin/client -c 10 -n 5 -q` -> 50 ok / 0 failed; show 11 threads with `ps -o nlwp`.
8. **Signals + stats** (1 min): `kill -USR1 <lb>` live stats, Ctrl+C graceful shutdown + final table + exit code 0.
9. **Code walkthrough** (rest): `net_utils.cpp` (syscalls), `thread_pool.cpp` (mutex/condvar), `backend_pool.cpp` (state machine), `signal_handler.cpp`.

## Optional: demo with the GUI
`make gui && ./bin/lb_gui`: press **Start**, click **Send Request** several times (round robin in the status bar), click **Stop** on a server row (UNHEALTHY within about one second),
click **Start** on that row (RECOVERING then HEALTHY), open **Server Status** (CPU/memory from `/proc`), finally **Stop**. See `docs/gui.md`.

## Likely questions and short answers
- *Why a mutex around the pool?* Workers, health thread and main share it; unsynchronised access is a data race.
- *Why condition variable?* Idle workers sleep (no busy waiting) until a client arrives.
- *Why `poll` timeouts?* To notice shutdown flags and to avoid blocking forever on a dead peer.
- *Why does the handler only set a flag?* Almost nothing else is async-signal-safe.
- *Process vs thread?* Backends are processes (isolation); LB workers are threads (cheap, shared memory).
- *What happens if all servers are down?* Client gets `ERROR: no healthy backend available`, request counted as failed, balancer keeps running and recovers automatically.
