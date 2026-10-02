# Makefile - Software-Defined Load Balancer & Health-Check Router
#   make            build everything into bin/
#   make clean      remove build/ and bin/
#   make unit-test  build + run the router unit test
#   make test       run the integration tests (tests/run_tests.sh)
#   make gui        build the native Linux GUI (needs libx11-dev libcairo2-dev, see README)
#   make run-gui    build everything + start the GUI
#   make gui-test   GUI controller integration test (no X server needed)
#
# Compiler flags (explained in docs/design.md too):
#   -std=c++17   use the C++17 standard (std::thread, std::mutex, <atomic>, ...)
#   -Wall -Wextra -Wpedantic   enable almost all warnings: catch bugs at compile time
#   -O2          optimise for speed without hurting debuggability too much
#   -g           include debug symbols so gdb / valgrind show file:line
#   -pthread     compile+link with POSIX threads support (needed by std::thread)
#   -Iinclude    look for headers in include/
#   -MMD -MP     auto-generate header dependency files (.d) so edits to headers rebuild

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -g -pthread -Iinclude -MMD -MP
LDFLAGS  := -pthread

BUILD := build
BIN   := bin

COMMON_OBJS := $(BUILD)/logger.o $(BUILD)/net_utils.o $(BUILD)/signal_handler.o
LB_OBJS := $(BUILD)/load_balancer.o $(BUILD)/admin_server.o $(BUILD)/router.o $(BUILD)/backend_pool.o \
           $(BUILD)/health_checker.o $(BUILD)/thread_pool.o $(BUILD)/statistics.o $(COMMON_OBJS)
BACKEND_OBJS := $(BUILD)/backend_server.o $(COMMON_OBJS)
CLIENT_OBJS  := $(BUILD)/client.o $(BUILD)/net_utils.o

# --- admin client / /proc monitor / GUI controller (plain C++, no GUI libraries needed) ---
CTL_OBJS  := $(BUILD)/lbctl.o $(BUILD)/admin_client.o $(BUILD)/proc_monitor.o $(BUILD)/net_utils.o
CORE4GUI  := $(BUILD)/gui_controller.o $(BUILD)/admin_client.o $(BUILD)/proc_monitor.o $(BUILD)/net_utils.o
GUI_OBJS  := $(BUILD)/gui_main.o $(CORE4GUI)
GUI_CFLAGS := $(shell pkg-config --cflags x11 cairo 2>/dev/null)
GUI_LIBS   := $(shell pkg-config --libs x11 cairo 2>/dev/null)

all: $(BIN)/load_balancer $(BIN)/backend_server $(BIN)/client $(BIN)/lbctl

$(BIN)/load_balancer: $(LB_OBJS) | $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/backend_server: $(BACKEND_OBJS) | $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/client: $(CLIENT_OBJS) | $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BIN)/lbctl: $(CTL_OBJS) | $(BIN)
	$(CXX) $^ -o $@ $(LDFLAGS)

# ---- native GUI (Xlib + Cairo). Optional: `make` alone never needs these libraries. ----
gui: $(BIN)/lb_gui

$(BIN)/lb_gui: $(GUI_OBJS) | $(BIN)
	@pkg-config --exists x11 cairo || { echo "Missing GUI libraries. Run: sudo apt install libx11-dev libcairo2-dev pkg-config"; exit 1; }
	$(CXX) $^ -o $@ $(LDFLAGS) $(GUI_LIBS)

$(BUILD)/gui_main.o: src/gui_main.cpp | $(BUILD)
	@pkg-config --exists x11 cairo || { echo "Missing GUI libraries. Run: sudo apt install libx11-dev libcairo2-dev pkg-config"; exit 1; }
	$(CXX) $(CXXFLAGS) $(GUI_CFLAGS) -c $< -o $@

$(BIN)/test_controller: tests/test_controller.cpp $(CORE4GUI) | $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN)/xclick: tests/xclick.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $(GUI_CFLAGS) $< -o $@ $(GUI_LIBS)

gui-test: all gui $(BIN)/test_controller $(BIN)/xclick
	bash tests/run_gui_tests.sh

run-gui: all gui
	./$(BIN)/lb_gui

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/test_router: tests/test_router.cpp $(BUILD)/router.o | $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

unit-test: $(BIN)/test_router
	./$(BIN)/test_router

test: all
	bash tests/run_tests.sh

$(BUILD) $(BIN):
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(BIN) run

.PHONY: all clean test unit-test gui gui-test run-gui
-include $(wildcard $(BUILD)/*.d)
