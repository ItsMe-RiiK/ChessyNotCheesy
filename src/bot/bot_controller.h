#ifndef CHESSY_NOT_CHEESY_BOT_CONTROLLER_H
#define CHESSY_NOT_CHEESY_BOT_CONTROLLER_H

#include "../capture/screen_capture.h"
#include "../capture/theme_manager.h"
#include "../chess/board_reader.h"
#include "../chess/game_state.h"
#include "../chess/stockfish.h"
#include "../driver/mouse.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

/*
 * BotController — Main orchestrator for the chess bot
 *
 * Runs the game loop: capture screen → read board → detect changes →
 * query Stockfish → execute move via driver mouse.
 */
class BotController
{
public:
  BotController();
  ~BotController();

  // Initialize all subsystems
  bool init();

  // Cleanup
  void cleanup();

  // Start/stop the bot loop
  void start();
  void stop();
  bool is_running() const;

  // Calibration
  void calibrate(int tl_x, int tl_y, int br_x, int br_y);
  bool is_calibrated() const;

  // Reset Game
  void reset_game();

  // Configuration
  void set_playing_white(bool white);
  void set_stockfish_depth(int depth);
  void set_move_delay(int min_ms, int max_ms);
  void set_poll_interval_ms(int ms);

  void set_status(const std::string& status);

  // Callbacks for GUI updates
  std::function<void(const std::string&)> on_status_change;
  std::function<void(const std::string&)> on_move_made;
  std::function<void(bool)>               on_color_detected;

private:
  // Subsystems
  ScreenCapture   capture_;
  ThemeManager    theme_manager_;
  BoardReader     board_reader_;
  GameState       game_state_;
  StockfishEngine stockfish_;
  VirtualMouse    mouse_;

  // Bot loop thread
  std::thread       bot_thread_;
  std::atomic<bool> running_;
  std::atomic<bool> should_stop_;

  // Configuration
  int stockfish_depth_;
  int move_delay_min_ms_;
  int move_delay_max_ms_;
  int poll_interval_ms_;

  // Status
  std::string current_status_;
  std::string engine_eval_;

  // Debounce tracking
  Board last_seen_board_;
  int   stable_frames_;

  // The main bot loop function
  void bot_loop();

  // Execute a UCI move on the board via mouse clicks
  bool execute_move(const std::string& uci_move);

  // Handle pawn promotion click
  bool click_promotion(char promo_piece);

  // Random delay for human-like behavior
  void human_delay();
};

#endif /* CHESSY_NOT_CHEESY_BOT_CONTROLLER_H */
