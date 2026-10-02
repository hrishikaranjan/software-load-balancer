# Native Linux GUI

A small desktop application written in **C++ only** (Xlib for the window and mouse events, Cairo for drawing).
No web browser, web server, HTML/CSS/JavaScript, Python or Java is involved. The GUI is only a *front-end*:
every number on the screen and every button is connected to the real load balancer, the real backend
processes, the real log file and the real Linux `/proc` filesystem.

Screenshots (rendered by the real program on a virtual X display, see `tests/run_gui_tests.sh`):
`docs/screenshots/gui_1_stopped.png`, `gui_2_running.png`, `gui_3_server2_down.png`.

## 1. Why Xlib + Cairo
| Option | Decision |
|---|---|
| GTK / Qt | Very common, but large frameworks (Qt needs its own meta-object compiler) and not verifiable in the build environment used for this project. |
| **Xlib + Cairo** | Native Linux, C/C++ libraries, two small apt packages, builds with plain `g++` + `make`. Every line is visible, which suits a system-programming viva. |
| Web UI | Not allowed by the project rules. |

## 2. How the GUI talks to the existing C++ core
```
                         +------------------------------ bin/lb_gui (GUI process) ------------------------------+
                         |  gui_main.cpp  : draws window, handles mouse clicks (Xlib + Cairo)                   |
                         |  gui_controller.cpp : all real work, no drawing (also used by tests/test_controller) |
                         +----+---------------+----------------+----------------+-------------------+-----------+
                              |               |                |                |                   |
   (1) admin UNIX socket      | (2) /proc     | (3) fork+exec  | (4) TCP client | (5) read file     |
   run/lb_admin.sock          | files         | + kill(signal) | to port 8080   | logs/load_balancer.log
                              v               v                v                v                   v
                      +--------------+   Linux kernel   +--------------+  +--------------+     +-----------+
                      | load_balancer|  (/proc stat,    | load_balancer|  | load_balancer|     | log file  |
                      | admin thread |   statm, ...)    | backend_server| | accept loop  |     | (Logger)  |
                      +--------------+                  +--------------+  +--------------+     +-----------+
```
| # | Channel | Used for | System calls / concepts |
|---|---|---|---|
| 1 | **UNIX domain socket** `run/lb_admin.sock` (mode 0600, owner only) | live status (`STATUS`), "Health Check" button (`CHECK`) | `socket(AF_UNIX)`, `bind`, `chmod`, `listen`, `accept`, `connect`; IPC |
| 2 | **/proc** | CPU %, memory (RSS), thread count of balancer and each backend; system CPU, memory, load average; finding processes by name | `/proc/<pid>/stat`, `statm`, `cmdline`, `/proc/stat`, `/proc/meminfo`, `/proc/loadavg`, `sysconf` |
| 3 | **Process control** | Start / Stop buttons | `fork`, `execv`, `dup2`, `waitpid`, `kill(SIGINT/SIGTERM)` |
| 4 | **TCP client socket** | "Send Request" button: a real request through the balancer | `socket`, `connect`, `send`, `recv` |
| 5 | **File reading** | "View Logs" | `ifstream`, `seekg` (reads the last 16 KB) |

### Admin protocol (text, one command per connection)
```
PING   -> PONG
CHECK  -> OK health check started          (health thread runs a probe round immediately)
STATUS -> OK
          pid=1234 / port=8080 / algorithm=round-robin / workers=8 / uptime=42
          total=.. / success=.. / failed=.. / active_clients=.. / healthy=.. / unhealthy=.. / recovering=..
          backend=<id>|<name>|<ip>|<port>|<STATUS>|<active>|<total>|<ok>|<failed>|<last_check_epoch>   (one per server)
          END
```
You can try it without the GUI: `./bin/lbctl status`, `./bin/lbctl check`, `./bin/lbctl proc <pid>`, `./bin/lbctl sys`.

## 3. What each part of the window shows (all real data)
| Window element | Source |
|---|---|
| RUNNING / STOPPED badge, PID, port, algorithm, workers, uptime | `STATUS` reply from the balancer |
| Total / Successful / Failed requests, Active clients | `Statistics` atomics inside the balancer (via `STATUS`) |
| Healthy backends x / n | `BackendPool` state (via `STATUS`) |
| Server rows: health, active, requests, ok, failed | `BackendPool` snapshot (via `STATUS`) - the same data as the statistics table printed at shutdown |
| Server rows: CPU %, MEM MB | `/proc/<backend pid>/stat` and `statm` |
| System line (CPU, memory, load, cores) | `/proc/stat`, `/proc/meminfo`, `/proc/loadavg` |
| Load balancer process line (CPU, memory, threads) | `/proc/<balancer pid>/stat`, `statm` |
| Logs panel | `logs/load_balancer.log` written by `logger.cpp` |
| Server Status panel | `STATUS` + `/proc` per backend |
| Status bar | result of the last button action (e.g. the real reply of a backend) |

## 4. Buttons
| Button | What it really does |
|---|---|
| **Start** | Starts every configured `bin/backend_server <id> <port>` that is not running, then `bin/load_balancer -i 1000` (health check every second). Output goes to `logs/gui_*.out`. |
| **Stop** | `SIGINT` to the balancer (identical to Ctrl+C: graceful shutdown + final statistics) and `SIGTERM` to the backends. |
| **Health Check** | Sends `CHECK` over the admin socket; the health thread probes all backends now. |
| **Send Request** | Opens a real TCP connection to the balancer port and sends `GUI test request #n`. Click repeatedly to watch round robin. The reply is shown in the status bar. |
| **Server Status / View Logs** | Switch the bottom panel. |
| Row **Stop / Start** | Stops (`SIGTERM`) or starts one backend: the quickest way to demonstrate failure detection and recovery. |

If processes were started from terminals instead, the GUI still works: it finds them through `/proc/*/cmdline`.
When the GUI window is closed it stops **only the processes it started itself**.

## 5. Design decisions
* **Core is unchanged in behaviour.** The balancer works exactly as before without the GUI. The only additions are an optional read-only admin thread (`admin_server.cpp`) and `HealthChecker::trigger_now()`. `make` and all earlier tests do not need GUI libraries.
* **View / controller split.** `gui_main.cpp` only draws and handles clicks. `gui_controller.cpp` contains the logic and has no X11 code, so it is tested automatically (`tests/test_controller.cpp`, 12 checks on real processes).
* **Single-threaded GUI** with `poll()` on the X connection and a 1-second refresh timer: no data races in the GUI.
* **Security:** the admin socket is `chmod 0600`, accepts only 3 fixed commands, and cannot change routing or kill anything.
* **Failure isolation:** if the GUI crashes, the balancer keeps running; if the balancer dies, the GUI shows STOPPED.

## 6. Build and run
```bash
sudo apt install -y build-essential pkg-config libx11-dev libcairo2-dev
make            # core programs (no GUI libraries needed)
make gui        # builds bin/lb_gui
./bin/lb_gui    # or: make run-gui        (needs a desktop session / DISPLAY)
```
Run `./bin/lb_gui` from a normal desktop terminal (Ubuntu desktop, VirtualBox/VMware desktop, WSLg). It finds the project folder from its own location.

## 7. Tests
```bash
make gui-test     # = bash tests/run_gui_tests.sh
```
1. `test_controller`: 12 checks - Start, requests, round robin, `/proc` values, stop/recover a backend, Health Check, log reading, Stop.
2. If `xvfb-run` and ImageMagick are installed (`sudo apt install xvfb imagemagick`): starts the real window on a virtual display, clicks the real buttons with `tests/xclick.cpp` and saves screenshots in `docs/screenshots/`.

## 8. Troubleshooting
| Problem | Fix |
|---|---|
| `Missing GUI libraries` during `make gui` | `sudo apt install libx11-dev libcairo2-dev pkg-config` |
| `Cannot open the X display` | You are in SSH/text mode. Use a desktop terminal, or `ssh -X`, or run `lbctl` instead |
| Window opens but everything says STOPPED | Press **Start**; check `bin/` was built with `make`; check ports 8080/9001-9003 are free |
| Start does nothing | `cat logs/gui_load_balancer.out` shows the reason (usually a port in use) |
| `Cannot connect` for Send Request | The balancer is not running yet (wait one second after Start) |

## 9. Honest scope notes
* The project contains **no Linux kernel module / device driver**. The "hardware/software interaction" part is shown through sockets, `/proc`, signals and CPU/memory monitoring. (A kernel module would need kernel headers, root privileges and extra risk; it is not required for this project.)
* `/proc` monitoring did not exist before this GUI step; it was added now (`proc_monitor.cpp`) so the CPU/memory fields are real.

## 10. Viva questions
* **How does the GUI know the balancer's numbers?** It asks over a UNIX domain socket; the balancer's admin thread copies the data under the same mutex the workers use.
* **Why a UNIX socket and not TCP?** Local-only, protected by file permissions, no port needed.
* **Where do CPU/memory values come from?** `/proc`: `utime+stime` ticks divided by elapsed time x `CLK_TCK`; RSS pages x page size.
* **What happens when I click Stop?** `kill(pid, SIGINT)` - the signal handler sets a flag, the accept loop leaves, workers finish, statistics are printed, exit code 0.
* **Does the load balancer depend on the GUI?** No. The GUI is optional and separate.
