// signal_handler.h - Signal setup. Handlers only set flags (async-signal-safe);
// the real work happens in normal code that polls these flags.
#pragma once

void install_signal_handlers();   // SIGINT/SIGTERM = shutdown, SIGUSR1 = user event, SIGPIPE ignored
bool shutdown_requested();
int  last_shutdown_signal();
bool consume_usr1();              // true once per SIGUSR1 received
