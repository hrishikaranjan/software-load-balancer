# Software-Defined Load Balancer & Health-Check Router

An individual capstone project written **only in C++ (C++17) for Linux**, using Linux system
programming: TCP sockets, processes, threads, mutexes/condition variables, signals, file
descriptors, `poll()`, and a Makefile build. No Python, Java, JavaScript, HTML/CSS, or web
framework is used anywhere. Shell scripts are used **only** to build, run and test.

## 1-3. Overview, problem statement, objectives
A single server is a single point of failure and a bottleneck. This project implements a
Layer-4-style **software load balancer**: clients talk to one address (port 8080); the balancer
spreads their requests over several backend server *processes* (ports 9001-9003), **detects
failed backends with health checks**, stops sending them traffic, and **detects when they recover**.

Objectives: demonstrate sockets, concurrency with synchronisation, signal handling, logging,
statistics, failure detection/recovery, testing, and clean modular design.

## 4. Features
- Backend server (`backend_server`): TCP server, returns its own ID, logs requests, can simulate failure (`kill -USR1`).
- Load balancer (`load_balancer`): accepts clients, forwards to a backend, returns the response.
- **Round Robin** routing that skips unhealthy servers; **Least Connections** also included (`-a lc`).
- **Health checker thread**: connect + `HEALTH`/`OK` handshake every N ms. States `HEALTHY -> UNHEALTHY -> RECOVERING -> HEALTHY`.
- **Passive failure detection + automatic retry** on another server if a backend dies between checks.
- **Thread pool** (producer/consumer queue) for concurrent clients.
- Graceful shutdown on SIGINT/SIGTERM; `SIGUSR1` prints live statistics.
- Thread-safe logging to `logs/load_balancer.log`; statistics summary at shutdown.
- Test client (`client`) with multi-request and multi-client (concurrent) modes.
- **Native Linux GUI (`lb_gui`, C++ with Xlib + Cairo)** showing live status, request counters, per-server health, CPU/memory from `/proc`, logs, with Start / Stop / Health Check / Send Request buttons. Optional; the core runs without it. See [docs/gui.md](docs/gui.md).
- `lbctl`: command-line tool for the same admin/`/proc` data.
- Config file `config/servers.conf` (no hard-coded backends).

## 5. Architecture
```
                   +----------------+
                   |     CLIENT     |
                   +-------+--------+
                           | TCP :8080
                           v
                +----------------------+
                |    LOAD BALANCER     |
                |----------------------|
                | Connection Manager   |  accept loop + thread pool
                | Health Checker       |  background thread
                | Router (RR / LC)     |  strategy objects
                | Statistics Manager   |  atomic counters
                | Logger               |  mutex-protected file
                +----+--------+--------+
                     |        |        \
              TCP    |        |         \ TCP
                     v        v          v
              +---------+ +---------+ +---------+
              |Backend 1| |Backend 2| |Backend 3|
              | :9001   | | :9002   | | :9003   |
              +---------+ +---------+ +---------+
```
Details: [docs/architecture.md](docs/architecture.md), [docs/design.md](docs/design.md).

## 6-8. Technologies and Linux requirements
C++17, POSIX/BSD sockets, `std::thread`/`std::mutex`/`std::condition_variable`/`std::atomic`,
`poll`, `fcntl`, `sigaction`, UNIX domain sockets, `/proc` monitoring, Xlib + Cairo (GUI only), GNU `make`, `g++` (>= 7). Linux only (Ubuntu 20.04+ / Debian / Fedora).
Optional monitoring tools: `ps top htop ss netstat lsof ip ping kill`.

## 9. Installation
```bash
sudo apt update && sudo apt install -y build-essential git     # g++ and make
# optional monitoring tools
sudo apt install -y htop net-tools lsof iproute2 iputils-ping

# GUI only (the core project does not need these):
sudo apt install -y pkg-config libx11-dev libcairo2-dev
# optional, for the automatic GUI screenshot test:
sudo apt install -y xvfb imagemagick
```

## 10. Compilation
```bash
make            # builds bin/load_balancer bin/backend_server bin/client
make clean      # removes build/ and bin/
make unit-test  # router unit test (no network)
make gui        # builds bin/lb_gui (needs libx11-dev libcairo2-dev)
```

## 11. How to run (4 terminals)
```bash
# Terminals 1-3: backends  (id, port)
./bin/backend_server 1 9001
./bin/backend_server 2 9002
./bin/backend_server 3 9003
# Terminal 4: load balancer  (options: ./bin/load_balancer -h)
./bin/load_balancer
# Terminal 5: client
./bin/client                       # interactive, one request
./bin/client -m Hello -n 9         # 9 requests -> shows round robin
./bin/client -m Hi -c 10 -n 5 -q   # 10 concurrent clients x 5 requests
```
Load balancer options: `-p port -c config -a rr|lc -i health_interval_ms -w workers -l logfile -s admin_socket`.

### Run with the GUI (one window instead of five terminals)
```bash
make && make gui
./bin/lb_gui          # or: make run-gui      (run from a desktop session)
```
Press **Start**, then **Send Request** repeatedly. Click **Stop** on a server row to simulate a crash, **Start** to recover it.

## 12. How to test
```bash
make unit-test        # Router unit test
make test             # all 6 integration tests (starts/stops all processes itself)
make gui-test         # GUI controller test on real processes (+ window test if xvfb/imagemagick installed)
bash tests/demo.sh    # presentation demo (add "pause" to wait for ENTER between steps)
```
Full test plan: [docs/testing.md](docs/testing.md). Real captured output: [docs/sample_output.txt](docs/sample_output.txt).

## 13. Failure simulation
```bash
ps aux | grep backend_server       # find PID of Server 2
kill 12345                         # (or kill -9) -> Server 2 goes UNHEALTHY within one interval
./bin/client -n 6 -m test          # only Servers 1 and 3 answer
./bin/backend_server 2 9002        # restart -> RECOVERING -> HEALTHY
kill -USR1 <backend-pid>           # alternative: toggle "simulated failure" (port closes, process lives)
kill -USR1 <lb-pid>                # print live statistics
kill -INT  <lb-pid>                # or Ctrl+C: graceful shutdown
```

## 14. Expected output (excerpt of `logs/load_balancer.log`)
```
[INFO] Load balancer started (pid=..., port=8080, algorithm=round-robin, workers=8)
[INFO] Backend Server 1 is healthy (127.0.0.1:9001)
[INFO] Client connected: 127.0.0.1:41234 (fd=7)
[INFO] Request routed to Server 2 (127.0.0.1:9002)
[WARNING] Backend Server 2 (127.0.0.1:9002) is UNHEALTHY
[INFO] Backend Server 2 is reachable again - RECOVERING (1/2)
[INFO] Backend Server 2 recovered and is HEALTHY again
[INFO] Load balancer shutting down (signal 2)
```
followed by the `LOAD BALANCER STATISTICS` table.

## 15. Project structure
```
software-load-balancer/
├── src/        load_balancer.cpp backend_server.cpp client.cpp router.cpp backend_pool.cpp
│               health_checker.cpp thread_pool.cpp statistics.cpp logger.cpp
│               signal_handler.cpp net_utils.cpp
│               admin_server.cpp admin_client.cpp proc_monitor.cpp lbctl.cpp      (admin channel + /proc)
│               gui_controller.cpp gui_main.cpp                                    (GUI)
├── include/    *.h (one per module + backend.h data model)
├── config/     servers.conf
├── tests/      run_tests.sh  demo.sh  test_router.cpp  invalid_backend.conf
│               run_gui_tests.sh  test_controller.cpp  xclick.cpp
├── docs/       architecture.md design.md testing.md gui.md sample_output.txt screenshots/ stages/stage1..6
├── logs/       runtime logs (git-ignored)
├── Makefile  README.md  .gitignore
```

## 16. Technical concepts demonstrated
| Concept | Where |
|---|---|
| TCP sockets: `socket bind listen accept connect send recv close` | `net_utils.cpp`, `load_balancer.cpp`, `backend_server.cpp` |
| File descriptors (sockets are fds; leaks avoided, `lsof`) | everywhere, see `close()` paths |
| Non-blocking I/O + `poll()` + timeouts | `net_utils.cpp` |
| Processes (3 backends + LB + client) | `backend_server.cpp`, `tests/*.sh` |
| Threads, thread pool, producer/consumer | `thread_pool.cpp` |
| Mutex, condition variable, atomics | `backend_pool.cpp`, `thread_pool.cpp`, `statistics.cpp` |
| Signals (`sigaction`, SIGINT/TERM/USR1/PIPE) | `signal_handler.cpp` |
| File handling, `mkdir`, `fopen`, `fflush` | `logger.cpp` |
| Health-check state machine | `backend_pool.cpp` |
| Strategy pattern (pluggable algorithms) | `router.cpp` |
| Byte order (`htons/htonl`) | `net_utils.cpp` |

## GUI: how it is connected to the core
The GUI (`bin/lb_gui`) is a separate process. It uses (1) a UNIX domain socket `run/lb_admin.sock` served by a small read-only admin thread inside the balancer,
(2) the Linux `/proc` filesystem for CPU/memory/thread numbers, (3) `fork/execv/kill` to start and stop the real programs, (4) a real TCP request for "Send Request",
(5) the real log file. Nothing on screen is dummy data. Full explanation, protocol, button mapping, viva questions: [docs/gui.md](docs/gui.md).
Screenshots: `docs/screenshots/`. Honest note: the project has no kernel module/device driver; hardware/software interaction is shown through sockets, signals and `/proc`.

## 17. Limitations
IPv4 only; text line protocol (one request/response per TCP connection, not HTTP); one backend
connection per request (no keep-alive pooling); no TLS; health thread probes sequentially;
statistics are in memory (reset on restart); the GUI needs an X11 session (Wayland desktops run it through XWayland); GUI refresh is once per second.

## 18. Future scope
Weighted round robin, IP-hash/sticky sessions, connection pooling, `epoll`-based event loop,
HTTP parsing, config reload on `SIGHUP`, IPv6, TLS, UNIX-socket admin interface.

## Git
Suggested commits: see `docs/stages/stage6_final.md`. Nothing has been pushed to GitHub by this
project; create your own repository and push yourself.
