#!/bin/bash
# ============================================================
# launcher.sh — Master launcher for ChessyNotCheesy
# 1. Verifies binary exists
# 2. Launches the chess bot seamlessly
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$SCRIPT_DIR/release/ChessyNotCheesy"

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  ChessyNotCheesy Launcher${NC}"
echo -e "${CYAN}========================================${NC}"

if [ ! -f "$BIN" ]; then
    echo -e "${RED}[ChessyNotCheesy] Binary not found at $BIN. Please ensure you ran install.sh or downloaded a valid release.${NC}"
    # Wait for user to read message before closing if launched from terminal (if applicable)
    sleep 3
    exit 1
fi

echo -e "${GREEN}[ChessyNotCheesy] Launching...${NC}"
echo -e "${CYAN}========================================${NC}"

if [ -f "$SCRIPT_DIR/.env" ]; then
    source "$SCRIPT_DIR/.env"
fi

cd "$SCRIPT_DIR"

# Check if we have permission to read input devices and write to uinput
HAS_PERMS=true

# Check global hotkey read access
for f in /dev/input/event*; do
    if [ ! -r "$f" ]; then
        HAS_PERMS=false
        break
    fi
done

# Check virtual mouse write access (uinput)
if [ ! -w "/dev/uinput" ]; then
    HAS_PERMS=false
fi

if [ "$HAS_PERMS" = true ]; then
    # Run natively without sudo (Wayland friendly!)
    env GDK_BACKEND=x11 "$BIN" "$@"
else
    echo -e "${YELLOW}[ChessyNotCheesy] Insufficient permissions for hotkeys or uinput. Elevating to sudo...${NC}"
    echo -e "${YELLOW}Tip: Run scripts/install.sh to configure udev rules and run without sudo!${NC}"
    
    if [ -n "$SUDO_PASS" ]; then
        xhost +si:localuser:root >/dev/null 2>&1 || true
        echo "$SUDO_PASS" | sudo -S --preserve-env=DISPLAY,XAUTHORITY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR env GDK_BACKEND=x11 "$BIN" "$@"
    else
        xhost +si:localuser:root >/dev/null 2>&1 || true
        sudo --preserve-env=DISPLAY,XAUTHORITY,WAYLAND_DISPLAY,XDG_RUNTIME_DIR env GDK_BACKEND=x11 "$BIN" "$@"
    fi
fi
