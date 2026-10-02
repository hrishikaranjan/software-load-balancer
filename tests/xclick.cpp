// xclick.cpp - test helper: sends a left mouse click to the GUI window (found by its title).
// Usage: xclick "<window title>" <x> <y>      (uses XSendEvent; no XTest extension needed)
#include <X11/Xlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <unistd.h>

static Window find_by_name(Display* d, Window w, const char* name) {
    char* n = nullptr;
    if (XFetchName(d, w, &n) && n) { bool ok = std::strcmp(n, name) == 0; XFree(n); if (ok) return w; }
    Window root, parent, *kids = nullptr; unsigned int count = 0;
    if (!XQueryTree(d, w, &root, &parent, &kids, &count)) return 0;
    Window found = 0;
    for (unsigned int i = 0; i < count && !found; ++i) found = find_by_name(d, kids[i], name);
    if (kids) XFree(kids);
    return found;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s title x y\n", argv[0]); return 1; }
    Display* d = XOpenDisplay(nullptr);
    if (!d) return 1;
    Window w = find_by_name(d, DefaultRootWindow(d), argv[1]);
    if (!w) { std::fprintf(stderr, "window '%s' not found\n", argv[1]); return 2; }
    for (int type : {ButtonPress, ButtonRelease}) {
        XEvent e; std::memset(&e, 0, sizeof e);
        e.xbutton.type = type; e.xbutton.window = w; e.xbutton.root = DefaultRootWindow(d);
        e.xbutton.x = std::atoi(argv[2]); e.xbutton.y = std::atoi(argv[3]);
        e.xbutton.button = Button1; e.xbutton.same_screen = True; e.xbutton.state = (type == ButtonRelease) ? Button1Mask : 0;
        XSendEvent(d, w, False, type == ButtonPress ? ButtonPressMask : ButtonReleaseMask, &e);
        XFlush(d); usleep(80000);
    }
    XCloseDisplay(d);
    return 0;
}
