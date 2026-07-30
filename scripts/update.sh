#!/bin/bash
# ============================================================
# update.sh — Update ChessyNotCheesy to latest release
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

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  ChessyNotCheesy Updater${NC}"
echo -e "${CYAN}========================================${NC}"

REPO="ItsMe-RiiK/ChessyNotCheesy"
LATEST_RELEASE_URL=$(curl -s https://api.github.com/repos/$REPO/releases/latest | grep "browser_download_url" | cut -d '"' -f 4 | grep 'zip')

if [ -z "$LATEST_RELEASE_URL" ]; then
    echo -e "${RED}[Update] Failed to fetch latest release URL. Check your internet connection or rate limits.${NC}"
    exit 1
fi

echo -e "${CYAN}[Update] Downloading latest release from $LATEST_RELEASE_URL...${NC}"
TMP_DIR=$(mktemp -d)
curl -L -o "$TMP_DIR/update.zip" "$LATEST_RELEASE_URL"

echo -e "${CYAN}[Update] Extracting update...${NC}"
unzip -q "$TMP_DIR/update.zip" -d "$TMP_DIR"

# The tarball extracts a folder named ChessyNotCheesy. We need to copy its contents into the current directory.
EXTRACTED_DIR="$TMP_DIR/ChessyNotCheesy"
if [ ! -d "$EXTRACTED_DIR" ]; then
    echo -e "${RED}[Update] Failed to locate extracted directory. Release format might have changed.${NC}"
    rm -rf "$TMP_DIR"
    exit 1
fi

echo -e "${CYAN}[Update] Overwriting current installation...${NC}"
# Temporarily kill the app if running
pkill -f "ChessyNotCheesy" || true

cp -r "$EXTRACTED_DIR"/* "$SCRIPT_DIR/"

rm -rf "$TMP_DIR"

echo -e "${GREEN}[Update] Update complete! Running the install script to ensure dependencies and shortcuts are fresh...${NC}"
bash "$SCRIPT_DIR/install.sh"
