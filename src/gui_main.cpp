// gui_main.cpp - Simple native Linux desktop GUI (C++, Xlib for the window/events, Cairo for drawing).
//
// This file only DRAWS and handles mouse clicks. All real work (talking to the load balancer,
// /proc monitoring, starting/stopping processes) is in gui_controller.cpp, which uses the same
// admin socket, signals and TCP sockets as the rest of the project. No web technology is used.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <libgen.h>
#include <limits.h>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>
#include "gui_controller.h"

namespace {

const int W = 900, H = 700;
const double kPi = 3.14159265358979323846;
enum ButtonId { B_START = 1, B_STOP, B_CHECK, B_REQUEST, B_STATUS, B_LOGS, B_ROW = 100 };

struct Color { double r, g, b; };
const Color BG = {0.925, 0.929, 0.937}, PANEL = {1, 1, 1}, BORDER = {0.78, 0.80, 0.84}, TEXT = {0.12, 0.14, 0.17},
            DIM = {0.42, 0.46, 0.51}, NAVY = {0.13, 0.20, 0.30}, GREEN = {0.14, 0.58, 0.28}, RED = {0.78, 0.20, 0.18},
            AMBER = {0.80, 0.48, 0.02}, BTN = {0.90, 0.91, 0.93}, BTN_HOVER = {0.83, 0.86, 0.91}, BTN_ON = {0.73, 0.79, 0.88};

struct Btn { int id; double x, y, w, h; std::string label; bool enabled; bool selected; };

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list a; va_start(a, f); vsnprintf(buf, sizeof buf, f, a); va_end(a);
    return buf;
}

void rrect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -kPi / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, kPi / 2);
    cairo_arc(cr, x + r, y + h - r, r, kPi / 2, kPi);
    cairo_arc(cr, x + r, y + r, r, kPi, 3 * kPi / 2);
    cairo_close_path(cr);
}
void use(cairo_t* cr, Color c) { cairo_set_source_rgb(cr, c.r, c.g, c.b); }
void panel(cairo_t* cr, double x, double y, double w, double h) {
    rrect(cr, x, y, w, h, 6); use(cr, PANEL); cairo_fill_preserve(cr); use(cr, BORDER); cairo_set_line_width(cr, 1); cairo_stroke(cr);
}
// align: 0 left, 1 centre, 2 right (relative to x)
void text(cairo_t* cr, double x, double y, const std::string& s, double size, bool bold, Color c, int align = 0, bool mono = false) {
    cairo_select_font_face(cr, mono ? "Monospace" : "Sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t e; cairo_text_extents(cr, s.c_str(), &e);
    double dx = align == 1 ? -e.x_advance / 2 : (align == 2 ? -e.x_advance : 0);
    use(cr, c); cairo_move_to(cr, x + dx, y); cairo_show_text(cr, s.c_str());
}
Color health_color(const std::string& h) {
    if (h == "HEALTHY") return GREEN;
    if (h == "UNHEALTHY") return RED;
    if (h == "RECOVERING") return AMBER;
    return DIM;
}
std::string hms(long s) { return fmt("%02ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60); }
std::string clock_of(long epoch) {
    if (epoch <= 0) return "never";
    char b[16]; struct tm tmv; time_t t = epoch; localtime_r(&t, &tmv); strftime(b, sizeof b, "%H:%M:%S", &tmv); return b;
}

struct Gui {
    Display* dpy = nullptr; Window win = 0; cairo_surface_t* surf = nullptr;
    Controller* ctl = nullptr;
    std::vector<Btn> btns;
    int tab = 0;                    // 0 = logs, 1 = server status
    int hover = 0, pressed = 0;
    bool quit = false;

    void add_button(cairo_t* cr, int id, double x, double y, double w, double h, const std::string& label, bool enabled, bool selected = false) {
        btns.push_back({id, x, y, w, h, label, enabled, selected});
        rrect(cr, x, y, w, h, 5);
        use(cr, !enabled ? BG : (pressed == id ? BTN_ON : (selected ? BTN_ON : (hover == id ? BTN_HOVER : BTN))));
        cairo_fill_preserve(cr); use(cr, BORDER); cairo_set_line_width(cr, 1); cairo_stroke(cr);
        text(cr, x + w / 2, y + h / 2 + 5, label, 13, selected, enabled ? TEXT : Color{0.65, 0.67, 0.70}, 1);
    }

    void draw() {
        const ViewModel& v = ctl->view();
        btns.clear();
        cairo_t* cr = cairo_create(surf);
        cairo_push_group(cr);
        use(cr, BG); cairo_paint(cr);

        // ---- title bar ----
        use(cr, NAVY); cairo_rectangle(cr, 0, 0, W, 64); cairo_fill(cr);
        text(cr, 20, 32, "SOFTWARE-DEFINED LOAD BALANCER", 21, true, {1, 1, 1});
        text(cr, 20, 52, "Health-Check Router  |  C++ / Linux system programming capstone", 11, false, {0.72, 0.80, 0.90});
        rrect(cr, W - 150, 17, 130, 30, 6); use(cr, v.lb_running ? GREEN : RED); cairo_fill(cr);
        text(cr, W - 85, 37, v.lb_running ? "RUNNING" : "STOPPED", 14, true, {1, 1, 1}, 1);

        // ---- server status line ----
        panel(cr, 20, 76, 860, 34);
        if (v.lb_running)
            text(cr, 32, 98, fmt("Load balancer: RUNNING    PID %d    Client port %d    Algorithm %s    Workers %d    Uptime %s",
                                 v.lb.pid, v.lb.port, v.lb.algorithm.c_str(), v.lb.workers, hms(v.lb.uptime).c_str()), 13, false, TEXT);
        else
            text(cr, 32, 98, "Load balancer: STOPPED    (press Start to launch the balancer and the backend servers)", 13, false, DIM);

        // ---- request statistics ----
        struct Stat { const char* label; std::string value; Color c; } stats[5] = {
            {"TOTAL REQUESTS", fmt("%llu", (unsigned long long)v.lb.total), TEXT},
            {"SUCCESSFUL", fmt("%llu", (unsigned long long)v.lb.success), GREEN},
            {"FAILED", fmt("%llu", (unsigned long long)v.lb.failed), v.lb.failed ? RED : TEXT},
            {"ACTIVE CLIENTS", fmt("%d", v.lb.active_clients), TEXT},
            {"HEALTHY BACKENDS", fmt("%d / %zu", v.lb.healthy, v.backends.size()), v.lb.healthy == (int)v.backends.size() && v.lb_running ? GREEN : (v.lb_running ? AMBER : DIM)}};
        for (int i = 0; i < 5; ++i) {
            double x = 20 + i * 174;
            panel(cr, x, 122, 164, 70);
            text(cr, x + 12, 144, stats[i].label, 10.5, true, DIM);
            text(cr, x + 12, 178, stats[i].value, 26, true, stats[i].c);
        }

        // ---- backend table ----
        size_t rows = v.backends.size() > 6 ? 6 : v.backends.size();
        double ty = 204, th = 34 + rows * 30 + 6;
        panel(cr, 20, ty, 860, th);
        const char* heads[] = {"SERVER", "ADDRESS", "HEALTH", "ACTIVE", "REQUESTS", "OK", "FAILED", "CPU %", "MEM MB"};
        double cx[] = {34, 110, 235, 360, 415, 490, 550, 615, 680};
        for (int i = 0; i < 9; ++i) text(cr, cx[i], ty + 22, heads[i], 10.5, true, DIM);
        for (size_t i = 0; i < rows; ++i) {
            const BackendView& b = v.backends[i];
            double ry = ty + 34 + i * 30;
            use(cr, BORDER); cairo_move_to(cr, 28, ry); cairo_line_to(cr, 872, ry); cairo_set_line_width(cr, 0.5); cairo_stroke(cr);
            std::string health = b.health;
            if (health == "-") health = b.proc_running ? "RUNNING" : "NOT RUNNING";
            Color hc = health_color(health);
            text(cr, cx[0], ry + 20, fmt("Server %d", b.cfg.id), 13, true, TEXT);
            text(cr, cx[1], ry + 20, fmt("%s:%d", b.cfg.ip.c_str(), b.cfg.port), 13, false, TEXT);
            cairo_arc(cr, cx[2] + 5, ry + 15, 5, 0, 2 * kPi); use(cr, hc); cairo_fill(cr);
            text(cr, cx[2] + 16, ry + 20, health, 13, true, hc);
            text(cr, cx[3], ry + 20, fmt("%d", b.active), 13, false, TEXT);
            text(cr, cx[4], ry + 20, fmt("%llu", b.total), 13, false, TEXT);
            text(cr, cx[5], ry + 20, fmt("%llu", b.ok), 13, false, GREEN);
            text(cr, cx[6], ry + 20, fmt("%llu", b.failed), 13, false, b.failed ? RED : TEXT);
            text(cr, cx[7], ry + 20, b.proc_running ? fmt("%.1f", b.proc.cpu_percent) : "-", 13, false, TEXT);
            text(cr, cx[8], ry + 20, b.proc_running ? fmt("%.1f", b.proc.rss_kb / 1024.0) : "-", 13, false, TEXT);
            add_button(cr, B_ROW + (int)i, 745, ry + 4, 100, 22, b.proc_running ? "Stop" : "Start", true);
        }

        // ---- system / process information (read from /proc) ----
        double sy = ty + th + 12;
        panel(cr, 20, sy, 860, 48);
        const SysInfo& s = v.sys;
        double used = (s.mem_total_kb - s.mem_available_kb) / 1048576.0, total = s.mem_total_kb / 1048576.0;
        text(cr, 32, sy + 20, fmt("System (/proc)    CPU %.1f%%    Memory %.1f / %.1f GB (%.0f%%)    Load average %.2f    CPU cores %d",
                                  s.cpu_percent, used, total, total > 0 ? 100.0 * used / total : 0.0, s.load1, s.cpus), 12, false, TEXT);
        if (v.lb_proc.valid)
            text(cr, 32, sy + 39, fmt("Load balancer process    CPU %.1f%%    Memory %.1f MB    Threads %d    PID %d",
                                      v.lb_proc.cpu_percent, v.lb_proc.rss_kb / 1024.0, v.lb_proc.threads, v.lb_proc.pid), 12, false, TEXT);
        else
            text(cr, 32, sy + 39, "Load balancer process: not running", 12, false, DIM);

        // ---- buttons ----
        double by = sy + 60;
        bool any_proc = v.lb_running || v.lb_proc.valid;
        for (const BackendView& b : v.backends) any_proc = any_proc || b.proc_running;
        bool all_up = v.lb_running;
        for (const BackendView& b : v.backends) all_up = all_up && b.proc_running;
        add_button(cr, B_START, 20, by, 100, 34, "Start", !all_up);
        add_button(cr, B_STOP, 130, by, 100, 34, "Stop", any_proc);
        add_button(cr, B_CHECK, 240, by, 120, 34, "Health Check", v.lb_running);
        add_button(cr, B_REQUEST, 370, by, 130, 34, "Send Request", v.lb_running);
        add_button(cr, B_STATUS, 540, by, 130, 34, "Server Status", true, tab == 1);
        add_button(cr, B_LOGS, 680, by, 110, 34, "View Logs", true, tab == 0);

        // ---- bottom panel: logs or detailed server status ----
        double py = by + 46, ph = H - 40 - py;
        panel(cr, 20, py, 860, ph);
        text(cr, 32, py + 21, tab == 0 ? "Logs  (logs/load_balancer.log)" : "Server status  (balancer admin socket + /proc)", 12, true, DIM);
        cairo_save(cr); cairo_rectangle(cr, 24, py + 28, 852, ph - 32); cairo_clip(cr);
        std::vector<std::string> lines;
        std::vector<Color> colors;
        if (tab == 0) {
            lines = v.log_lines;
            if (lines.empty()) lines.push_back("(no log yet - press Start)");
            for (const std::string& l : lines)
                colors.push_back(l.find("[WARNING]") != std::string::npos ? AMBER : (l.find("[ERROR]") != std::string::npos ? RED : TEXT));
        } else {
            if (!v.lb_running) lines.push_back("Load balancer is not running.");
            else lines.push_back(fmt("Balancer  pid %d  port %d  %s  workers %d  healthy %d  unhealthy %d  recovering %d",
                                     v.lb.pid, v.lb.port, v.lb.algorithm.c_str(), v.lb.workers, v.lb.healthy, v.lb.unhealthy, v.lb.recovering));
            for (const BackendView& b : v.backends) {
                std::string proc = b.proc_running ? fmt("pid %d  CPU %.1f%%  RSS %.1f MB  threads %d", b.proc.pid, b.proc.cpu_percent, b.proc.rss_kb / 1024.0, b.proc.threads) : "process not running";
                lines.push_back(fmt("Server %d  %s:%d  %-10s  active %d  last check %s", b.cfg.id, b.cfg.ip.c_str(), b.cfg.port, b.health.c_str(), b.active, clock_of(b.last_check).c_str()));
                lines.push_back("          " + proc);
            }
            for (size_t i = 0; i < lines.size(); ++i) colors.push_back(i == 0 ? DIM : TEXT);
        }
        int maxl = (int)((ph - 38) / 15);
        size_t first = lines.size() > (size_t)maxl ? lines.size() - maxl : 0;
        for (size_t i = first; i < lines.size(); ++i)
            text(cr, 32, py + 45 + (i - first) * 15, lines[i], 11, false, colors[i], 0, true);
        cairo_restore(cr);

        // ---- status bar ----
        use(cr, NAVY); cairo_rectangle(cr, 0, H - 32, W, 32); cairo_fill(cr);
        text(cr, 20, H - 11, "Status: " + ctl->status_line(), 13, false, {1, 1, 1});

        cairo_pop_group_to_source(cr); cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_flush(surf); XFlush(dpy);
    }

    int hit(int x, int y) const {
        for (const Btn& b : btns)
            if (b.enabled && x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return b.id;
        return 0;
    }

    void act(int id) {
        const ViewModel& v = ctl->view();
        switch (id) {
            case B_START: ctl->start_all(); break;
            case B_STOP: ctl->stop_all(); break;
            case B_CHECK: ctl->health_check(); break;
            case B_REQUEST: ctl->send_test_request(); break;
            case B_STATUS: tab = 1; break;
            case B_LOGS: tab = 0; break;
            default:
                if (id >= B_ROW && (size_t)(id - B_ROW) < v.backends.size()) {
                    size_t i = id - B_ROW;
                    if (v.backends[i].proc_running) ctl->stop_backend(i); else ctl->start_backend(i);
                }
        }
    }
};

std::string find_project_root(int argc, char** argv) {
    if (argc > 1) return argv[1];
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);       // where is this program on disk?
    if (n <= 0) return ".";
    exe[n] = 0;
    std::string bin_dir = dirname(exe);                                // .../project/bin
    return bin_dir + "/..";
}

}  // namespace

int main(int argc, char** argv) {
    Controller ctl(find_project_root(argc, argv));
    Gui gui; gui.ctl = &ctl;

    gui.dpy = XOpenDisplay(nullptr);
    if (!gui.dpy) { std::fprintf(stderr, "Cannot open the X display. Run this from a desktop session (DISPLAY is not set).\n"); return 1; }
    int scr = DefaultScreen(gui.dpy);
    gui.win = XCreateSimpleWindow(gui.dpy, RootWindow(gui.dpy, scr), 0, 0, W, H, 0, 0, WhitePixel(gui.dpy, scr));
    XStoreName(gui.dpy, gui.win, "Software-Defined Load Balancer");
    XSizeHints hints; std::memset(&hints, 0, sizeof hints);
    hints.flags = PMinSize | PMaxSize; hints.min_width = hints.max_width = W; hints.min_height = hints.max_height = H;
    XSetWMNormalHints(gui.dpy, gui.win, &hints);
    Atom del = XInternAtom(gui.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(gui.dpy, gui.win, &del, 1);
    XSelectInput(gui.dpy, gui.win, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask);
    XMapWindow(gui.dpy, gui.win);
    gui.surf = cairo_xlib_surface_create(gui.dpy, gui.win, DefaultVisual(gui.dpy, scr), W, H);

    using clk = std::chrono::steady_clock;
    auto last_refresh = clk::now() - std::chrono::seconds(10);
    bool dirty = true;
    ctl.refresh();

    while (!gui.quit) {
        while (XPending(gui.dpy)) {
            XEvent e; XNextEvent(gui.dpy, &e);
            switch (e.type) {
                case Expose: dirty = true; break;
                case MotionNotify: { int h = gui.hit(e.xmotion.x, e.xmotion.y); if (h != gui.hover) { gui.hover = h; dirty = true; } break; }
                case LeaveNotify: gui.hover = 0; dirty = true; break;
                case ButtonPress: if (e.xbutton.button == Button1) { gui.pressed = gui.hit(e.xbutton.x, e.xbutton.y); dirty = true; } break;
                case ButtonRelease:
                    if (e.xbutton.button == Button1) {
                        int h = gui.hit(e.xbutton.x, e.xbutton.y);
                        int p = gui.pressed; gui.pressed = 0; dirty = true;
                        if (h && h == p) { gui.act(h); last_refresh = clk::now() - std::chrono::milliseconds(800); }
                    }
                    break;
                case ClientMessage:
                    if ((Atom)e.xclient.data.l[0] == del) gui.quit = true;
                    break;
            }
        }
        auto now = clk::now();
        long since = (long)std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refresh).count();
        if (since >= 1000) { ctl.refresh(); last_refresh = now; dirty = true; since = 0; }
        if (dirty) { gui.draw(); dirty = false; }
        pollfd p = {ConnectionNumber(gui.dpy), POLLIN, 0};
        poll(&p, 1, (int)(1000 - since) > 20 ? (int)(1000 - since) : 20);      // sleep until an X event or the next refresh
    }

    ctl.shutdown_children();                 // stop only what this GUI started
    cairo_surface_destroy(gui.surf);
    XDestroyWindow(gui.dpy, gui.win);
    XCloseDisplay(gui.dpy);
    return 0;
}
