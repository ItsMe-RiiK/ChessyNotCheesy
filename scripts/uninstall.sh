#!/bin/bash
# ============================================================
# uninstall.sh — Remove ChessyNotCheesy from Desktop
# ============================================================

set -e

CYAN='\033[0;36m'
GREEN='\033[0;32m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$(basename "$SCRIPT_DIR")" = "scripts" ]; then
    SCRIPT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
fi

DESKTOP_DIR="$HOME/Desktop"
DESKTOP_FILE="$DESKTOP_DIR/ChessyNotCheesy.desktop"
ALT_DESKTOP_FILE="$HOME/.local/share/applications/ChessyNotCheesy.desktop"

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  ChessyNotCheesy Uninstaller${NC}"
echo -e "${CYAN}========================================${NC}"

if [ -f "$DESKTOP_FILE" ]; then
    rm "$DESKTOP_FILE"
    echo -e "${GREEN}[Uninstall] Removed desktop shortcut from $DESKTOP_FILE.${NC}"
fi

if [ -f "$ALT_DESKTOP_FILE" ]; then
    rm "$ALT_DESKTOP_FILE"
    echo -e "${GREEN}[Uninstall] Removed desktop shortcut from $ALT_DESKTOP_FILE.${NC}"
fi

UDEV_RULE_FILE="/etc/udev/rules.d/99-chessynotcheesy.rules"
MODULES_LOAD_FILE="/etc/modules-load.d/uinput.conf"
if [ -f "$UDEV_RULE_FILE" ]; then
    echo -e "${CYAN}[Uninstall] Removing udev rules for mouse/keyboard inputs...${NC}"
    if [ -n "$SUDO_PASS" ]; then
        echo "$SUDO_PASS" | sudo -S rm -f "$UDEV_RULE_FILE" "$MODULES_LOAD_FILE"
        echo "$SUDO_PASS" | sudo -S udevadm control --reload-rules
        echo "$SUDO_PASS" | sudo -S udevadm trigger
    else
        sudo rm -f "$UDEV_RULE_FILE" "$MODULES_LOAD_FILE"
        sudo udevadm control --reload-rules
        sudo udevadm trigger
    fi
    echo -e "${GREEN}[Uninstall] udev rules removed.${NC}"
fi

ICON_FILE="$HOME/.local/share/icons/hicolor/256x256/apps/chessynotcheesy.png"
if [ -f "$ICON_FILE" ]; then
    echo -e "${CYAN}[Uninstall] Removing application icon...${NC}"
    rm -f "$ICON_FILE"
    gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
    echo -e "${GREEN}[Uninstall] Icon removed.${NC}"
fi

echo -e "${GREEN}[Uninstall] Uninstallation complete.${NC}"
exit 0
