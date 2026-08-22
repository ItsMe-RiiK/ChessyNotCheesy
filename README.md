# ChessyNotCheesy

<p align="center">
    <img src="images/Icon_256.png" alt="Logo" width="125"/>
</p>

**ChessyNotCheesy** is a highly optimized, fully autonomous computer-vision based chess bot designed to play on Chess.com directly from your Linux desktop, powered by the **Stockfish** engine.

Unlike traditional chess bots or browser extensions that inject JavaScript, read browser memory, or hook into the DOM, **ChessyNotCheesy operates entirely outside the browser**. It acts exactly like a human player: it "looks" at your screen using native screen capture (`XShm` or `grim`) and "clicks" the mouse using hardware-level input simulation (a `/dev/uinput` virtual tablet driver).

---

## 📸 Preview
<summary>
<div align="center">
  <b>Calibration & Setup</b><br>
  <img src="images/Setup.gif" alt="Setup" width="600"/><br><br>

  <b>In-Match Gameplay</b><br>
  <img src="images/Play.gif" alt="Play" width="600"/>
</div>

## ✨ Features

- **100% External Vision System**: Uses OpenCV `TM_SQDIFF_NORMED` template matching to read the board visually. Completely immune to board highlights, last-move indicators, and square colors.
- **Universal Linux Support**: Fully supports both **X11** and **Wayland** display servers dynamically out-of-the-box!
- **CPU Efficient**: Built on lightweight native APIs (`XShm` on X11, `grim` on Wayland) and heavily optimized C++ OpenCV processing.
- **Human-like Interaction**: Uses a custom `/dev/uinput` absolute pointer device to simulate realistic, hardware-level mouse movements and clicks.
- **Built-in GTK3 GUI**:
  - Real-time PGN generation and tracking.
  - Live engine evaluation (e.g., `eval: +1.20`).
  - Adjustable Stockfish calculation depth (defaults to 4).
  - Configurable artificial delay between moves to mimic human thinking time.
  - Global Hotkeys: `` ` `` to start/stop, `C` to calibrate, `R` to reset game memory, `1` for White, `2` for Black, `SHIFT + -/+` to adjust min mouse delay, `CTRL + -/+` to adjust max mouse delay.
- **Robust State Engine**: Handles move parsing, en-passant, and castling rules seamlessly without parsing algebraic notation from the website DOM.

---

## 🛠️ Requirements & Dependencies
<details>
<summary><b>Requirements</b></summary>

### 🖥️ Requirements
This project is built exclusively for Linux and is fully tested on both **Arch Linux / X11** and **Arch Linux / Wayland (e.g. labwc, sway, hyprland)**. 
</details>

<details>
<summary><b>Dependencies</b></summary>

### ⛓️ Dependencies
To compile and run this project, the following packages must be installed:
- `g++` (Compiler with C++17 support)
- `make`
- `stockfish` (Required for move calculation)
- `opencv` (libopencv-dev, specifically OpenCV 5 or compatible)
- `gtk3`
- `x11` and `xtst` (libxtst-dev)
- `grim` and `slurp` (Required for Wayland screen capture and calibration)

**Automated Installation (Arch Linux Only):**
An automated script is provided to install all necessary dependencies and system permissions:
```bash
./scripts/install.sh
```
*(If you are on Debian/Ubuntu or Fedora, you must manually install the equivalent packages (`build-essential`, `libopencv-dev`, `libxtst-dev`, `grim`, `slurp`, etc) and create your own udev rules).*
</details>

---
## 🚀 Getting Started
<details>
<summary><b>Option 1: Pre-compiled Releases (Recommended)</b></summary>

Whenever a new version is released, an automated GitHub Action compiles it and packages it. You can download the latest `.zip` archive directly from the [Releases page](https://github.com/ItsMe-RiiK/ChessyNotCheesy/releases).

1. Download `ChessyNotCheesy-linux-x86_64.zip`.
2. Extract the archive.
3. Run the installer script to set up dependencies, udev permissions (so the bot doesn't need `sudo`), and create a Desktop shortcut:
```bash
cd ChessyNotCheesy
./scripts/install.sh
```
4. You can now launch **ChessyNotCheesy** from your Desktop application menu
</details>

<details>
<summary><b>Option 2: Build from Source</b></summary>

1. Clone the repository:
```bash
git clone https://github.com/ItsMe-RiiK/ChessyNotCheesy.git
cd ChessyNotCheesy
```
2. Run the installer to setup dependencies and udev permissions:
```bash
./scripts/install.sh
```
3. Compile the project:
```bash
make -j$(nproc)
```
4. Launch the bot:
```bash
./launcher.sh
```
</details>

---
## 🖥️ Application Scripts
We provide convenient scripts to manage your installation (located in the `scripts/` folder):

- **Install/Setup (`install.sh`)**: Installs Arch Linux dependencies, configures `udev` rules so the bot can listen to global hotkeys without requiring `sudo` on every launch, and creates a pure GUI desktop shortcut.
- **Update (`update.sh`)**: Automatically fetches the latest `.zip` release from GitHub, downloads it, extracts it, and overwrites your current installation seamlessly.
- **Uninstall (`uninstall.sh`)**: Removes the desktop shortcuts and cleans up the `udev` rules from your system.

---
## 🎮 How to Use

1. **Setup Chess.com**: 
   - Open a game on Chess.com.
   - **Important:** Set your piece style to **Neo** and board style to **Wood**. If you prefer other styles, you must update the templates in `themes/pieces/(add new themes folder for pieces here or board in board folder)` and adjust the [Default Config](themes/default.cfg).
   - Set move method to **Drag or Click**.
   - Set piece animation to **Slow** or **Medium** (Default).
   - Ensure the entire board is visible on your primary monitor without obstruction.

2. **Calibrate**:
   - In the GUI, click `(C)alibrate` or press the `C` hotkey.
   - **On X11**: Click exactly on the **Top-Left corner** of the board (the top-left edge of the `a8` square), then click the **Bottom-Right corner** (the bottom-right edge of the `h1` square).
   - **On Wayland**: Your screen will freeze. Click and drag a selection box perfectly over the entire chessboard to select the region.
   - The bot will automatically calculate square sizes and lock its vision onto the board.

3. **Play**:
   - Select your color side (`1` for White, `2` for Black).
   - Press the backtick `` ` `` key to start the bot.
   - Watch it play automatically!
   - When the game ends, press `R` to reset the bot's memory for the next game.

---

## ⚠️ Disclaimer

This project is strictly for **educational and research purposes**. Using automated bots on Chess.com violates their Terms of Service and will likely result in your account being banned. The developers hold no responsibility for any consequences that arise from using this software. **Do not use this tool to cheat against human players.**

---

## 📝 License

This project is licensed under the GPL-3.0 License - see the [LICENSE](License) file for details.