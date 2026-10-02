#!/usr/bin/env bash
# GUI tests.
#  1. Controller test (always): real processes, no display required.
#  2. Window test (only if Xvfb + ImageMagick 'import' are installed): starts bin/lb_gui on a virtual
#     X display, clicks the real [Start] button with tests/xclick, takes a screenshot into docs/screenshots/.
cd "$(dirname "$0")/.."
BIN=bin
port_open() { (exec 3<>/dev/tcp/127.0.0.1/"$1") 2>/dev/null; }
if port_open 8080 || port_open 9001 || port_open 9002 || port_open 9003; then
    echo "Ports 8080/9001-9003 already in use. Stop other instances first."; exit 2
fi
$BIN/test_controller . || exit 1

if [ -x $BIN/lb_gui ] && command -v xvfb-run >/dev/null && command -v import >/dev/null && [ -x $BIN/xclick ]; then
    echo "=== Window test on a virtual X display (Xvfb) ==="
    mkdir -p docs/screenshots
    xvfb-run -a -s "-screen 0 1000x760x24" bash -c '
        '$BIN'/lb_gui & GUI=$!
        sleep 1.5
        import -window root docs/screenshots/gui_1_stopped.png
        '$BIN'/xclick "Software-Defined Load Balancer" 70 423     # [ Start ]
        sleep 5
        '$BIN'/xclick "Software-Defined Load Balancer" 435 423    # [ Send Request ] x3
        '$BIN'/xclick "Software-Defined Load Balancer" 435 423
        '$BIN'/xclick "Software-Defined Load Balancer" 435 423
        sleep 1.5
        import -window root docs/screenshots/gui_2_running.png
        '$BIN'/xclick "Software-Defined Load Balancer" 795 283    # Server 2 row [ Stop ]
        sleep 4
        '$BIN'/xclick "Software-Defined Load Balancer" 605 423    # [ Server Status ]
        sleep 1.5
        import -window root docs/screenshots/gui_3_server2_down.png
        '$BIN'/xclick "Software-Defined Load Balancer" 170 423    # [ Stop ]
        sleep 4
        kill $GUI; wait $GUI 2>/dev/null
    '
    for f in docs/screenshots/gui_*.png; do convert "$f" -crop 900x700+0+0 +repage "$f"; done
    pkill -f bin/backend_server 2>/dev/null; pkill -f bin/load_balancer 2>/dev/null
    ls -l docs/screenshots/*.png
else
    echo "(window test skipped: needs bin/lb_gui, bin/xclick, xvfb-run and ImageMagick 'import')"
fi
