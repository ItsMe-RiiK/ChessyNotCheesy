#!/bin/bash
# ============================================================
# install.sh — Install ChessyNotCheesy to Desktop
# ============================================================

set -e

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ "$(basename "$SCRIPT_DIR")" = "scripts" ]; then
    SCRIPT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
fi

DESKTOP_DIR="$HOME/Desktop"
DESKTOP_FILE="$DESKTOP_DIR/ChessyNotCheesy.desktop"

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  ChessyNotCheesy Installer${NC}"
echo -e "${CYAN}========================================${NC}"

echo -e "${CYAN}[Install] Installing system dependencies (Stockfish, OpenCV, etc)...${NC}"
if [ -n "$SUDO_PASS" ]; then
    echo "$SUDO_PASS" | sudo -S pacman -S --noconfirm base-devel git gtk3 opencv qt6-base libxtst stockfish grim slurp 2>/dev/null || true
else
    sudo pacman -S --noconfirm base-devel git gtk3 opencv qt6-base libxtst stockfish grim slurp 2>/dev/null || true
fi

echo -e "${CYAN}[Install] Setting up udev rules for mouse/keyboard inputs without sudo...${NC}"
UDEV_RULE_FILE="/etc/udev/rules.d/99-chessynotcheesy.rules"
UDEV_RULE_CONTENT="SUBSYSTEM==\"input\", GROUP=\"input\", MODE=\"0666\""
if [ -n "$SUDO_PASS" ]; then
    echo "$SUDO_PASS" | sudo -S bash -c "echo '$UDEV_RULE_CONTENT' > $UDEV_RULE_FILE"
    echo "$SUDO_PASS" | sudo -S udevadm control --reload-rules
    echo "$SUDO_PASS" | sudo -S udevadm trigger
else
    sudo bash -c "echo '$UDEV_RULE_CONTENT' > $UDEV_RULE_FILE"
    sudo udevadm control --reload-rules
    sudo udevadm trigger
fi
echo -e "${GREEN}[Install] Permissions granted!${NC}"

if [ ! -d "$DESKTOP_DIR" ]; then
    echo -e "${YELLOW}[Install] Desktop directory not found at $DESKTOP_DIR. Using standard applications folder...${NC}"
    DESKTOP_DIR="$HOME/.local/share/applications"
    DESKTOP_FILE="$DESKTOP_DIR/ChessyNotCheesy.desktop"
    mkdir -p "$DESKTOP_DIR"
fi

echo -e "${CYAN}[Install] Installing application icon...${NC}"
ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
mkdir -p "$ICON_DIR"
cp "$SCRIPT_DIR/images/Icon_256.png" "$ICON_DIR/chessynotcheesy.png"
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

echo -e "${GREEN}[Install] Creating desktop shortcut at $DESKTOP_FILE...${NC}"

cat > "$DESKTOP_FILE" << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=ChessyNotCheesy
Comment=Autonomous computer-vision chess bot
Exec=bash "$SCRIPT_DIR/launcher.sh"
Icon=chessynotcheesy
Terminal=false
Categories=Game;BoardGame;
EOF

chmod +x "$DESKTOP_FILE"
gio set "$DESKTOP_FILE" metadata::trusted true 2>/dev/null || true

echo -e "${GREEN}[Install] Done! You can now launch ChessyNotCheesy purely from the GUI.${NC}"
exit 0
