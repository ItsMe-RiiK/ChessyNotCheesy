#ifndef CHESSY_NOT_CHEESY_STOCKFISH_H
#define CHESSY_NOT_CHEESY_STOCKFISH_H

#include <future>
#include <string>

/*
 * StockfishEngine — UCI protocol communication with Stockfish
 *
 * Spawns a Stockfish process and communicates via stdin/stdout pipes
 * using the Universal Chess Interface (UCI) protocol.
 */
class StockfishEngine
{
public:
  StockfishEngine();
  ~StockfishEngine();

  // Start the Stockfish process
  bool start(const std::string& path = "stockfish");

  // Stop the Stockfish process
  void stop();

  // Is the engine running?
  bool is_running() const;

  // Set position from FEN string
  bool set_position(const std::string& fen);

  // Get best move at given depth (blocking)
  // Returns move in UCI format (e.g., "e2e4", "e7e8q" for promotion)
  std::string get_best_move(int depth = 20);

  // Stop the current search immediately and force engine to return bestmove
  void stop_search();

  // Get best move at given depth asynchronously (non-blocking)
  std::future<std::string> get_best_move_async(int depth = 20);

  // Configure engine options
  bool set_option(const std::string& name, const std::string& value);
  bool set_threads(int n);
  bool set_hash(int mb);

  // Get engine info string (name, version)
  std::string get_engine_info() const;

  // Get the evaluation score from last search (in centipawns)
  int get_last_score() const;

  // Get the principal variation from last search
  std::string get_last_pv() const;

private:
  pid_t child_pid_;
  int   to_engine_fd_;    // pipe: write to Stockfish's stdin
  int   from_engine_fd_;  // pipe: read from Stockfish's stdout

  std::string engine_info_;
  std::string engine_path_;
  int         last_score_;
  std::string last_pv_;
  std::string read_buffer_;

  // Send a command to the engine
  bool send_command(const std::string& cmd);

  // Wait for a specific response prefix, return the full line
  std::string wait_for(const std::string& prefix, int timeout_ms = 10000);

  // Parse info lines from search output
  void parse_info_line(const std::string& line);
};

#endif /* CHESSY_NOT_CHEESY_STOCKFISH_H */
