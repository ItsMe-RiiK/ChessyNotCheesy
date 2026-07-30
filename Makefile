CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
# Always build to local release folder
RELEASE_DIR := Release
SRC_DIR := src
BUILD_DIR := build
TARGET := $(RELEASE_DIR)/ChessyNotCheesy

# Source files
SRCS := $(SRC_DIR)/main.cpp \
        $(SRC_DIR)/driver/mouse.cpp \
        $(SRC_DIR)/capture/screen_capture.cpp \
        $(SRC_DIR)/capture/theme_manager.cpp \
        $(SRC_DIR)/chess/board_reader.cpp \
        $(SRC_DIR)/chess/stockfish.cpp \
        $(SRC_DIR)/chess/game_state.cpp \
        $(SRC_DIR)/bot/bot_controller.cpp \
        $(SRC_DIR)/gui/gui.cpp

OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

# Libraries
# GTK3 for GUI
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

# X11 + XShm for screen capture
X11_LIBS := -lX11 -lXext -lXtst

# OpenCV for template matching
OPENCV_CFLAGS := $(shell pkg-config --cflags opencv5)
OPENCV_LIBS := -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_highgui -ljxl -ljxl_threads

# Threads
THREAD_LIBS := -pthread

ALL_CFLAGS := $(CXXFLAGS) $(GTK_CFLAGS) $(OPENCV_CFLAGS)
ALL_LIBS := $(GTK_LIBS) $(X11_LIBS) $(OPENCV_LIBS) $(THREAD_LIBS)

.PHONY: all clean run test-stockfish test-driver setup-stockfish

all: $(TARGET)

setup-stockfish:
	@echo "Running Stockfish setup script..."
	@./scripts/setup_stockfish.sh

$(TARGET): $(OBJS)
	@mkdir -p $(RELEASE_DIR)
	$(CXX) $(ALL_CFLAGS) -o $@ $^ $(ALL_LIBS)
	@echo ""
	@echo "✓ Build complete: $(TARGET)"
	@echo "  Run with: sudo -E $(TARGET)"
	@echo "  Or use:   ./launcher.sh"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
	@echo "✓ Cleaned."

-include .env

run: $(TARGET)
	@if [ -n "$(SUDO_PASS)" ]; then \
		echo "$(SUDO_PASS)" | sudo -S --preserve-env=DISPLAY,XAUTHORITY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR $(TARGET); \
	else \
		sudo --preserve-env=DISPLAY,XAUTHORITY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR $(TARGET); \
	fi

test-stockfish: $(TARGET)
	$(TARGET) --test-stockfish

test-driver: $(TARGET)
	@if [ -n "$(SUDO_PASS)" ]; then \
		echo "$(SUDO_PASS)" | sudo -S --preserve-env=DISPLAY,XAUTHORITY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR $(TARGET) --test-driver; \
	else \
		sudo --preserve-env=DISPLAY,XAUTHORITY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR $(TARGET) --test-driver; \
	fi
