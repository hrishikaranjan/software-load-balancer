#include "signal_handler.h"
#include <csignal>
#include <cstring>

namespace {
// volatile sig_atomic_t is the only type the C standard guarantees safe to write in a handler.
volatile sig_atomic_t g_shutdown = 0;
volatile sig_atomic_t g_usr1 = 0;
volatile sig_atomic_t g_last_signal = 0;

void handler(int sig) {
    if (sig == SIGUSR1) { g_usr1 = 1; return; }
    g_last_signal = sig;
    g_shutdown = 1;
}
}  // namespace

void install_signal_handlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;               // no SA_RESTART: blocking calls return EINTR so loops notice
    sigaction(SIGINT, &sa, nullptr);    // Ctrl+C
    sigaction(SIGTERM, &sa, nullptr);   // kill <pid>
    sigaction(SIGUSR1, &sa, nullptr);   // kill -USR1 <pid>

    struct sigaction ign;
    std::memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;      // writing to a closed socket must not kill the process
    sigemptyset(&ign.sa_mask);
    sigaction(SIGPIPE, &ign, nullptr);
}

bool shutdown_requested() { return g_shutdown != 0; }
int last_shutdown_signal() { return static_cast<int>(g_last_signal); }
bool consume_usr1() {
    if (g_usr1) { g_usr1 = 0; return true; }
    return false;
}
