#include "stockfish.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

StockfishEngine::StockfishEngine() :
    child_pid_(-1),
    to_engine_fd_(-1),
    from_engine_fd_(-1),
    last_score_(0)
{
}

StockfishEngine::~StockfishEngine() { stop(); }

bool StockfishEngine::start(const std::string& path)
{
  if (child_pid_ > 0)
    return true;  // Already running

  std::string actual_path = path;
  if (path == "stockfish" && access("bin/stockfish", X_OK) == 0) {
    actual_path = "bin/stockfish";
  }

  engine_path_ = actual_path;

  int to_engine[2];    // parent writes → child reads (stdin)
  int from_engine[2];  // child writes → parent reads (stdout)

  if (pipe(to_engine) < 0) {
    perror("[Stockfish] pipe() failed");
    return false;
  }

  if (pipe(from_engine) < 0) {
    perror("[Stockfish] pipe() failed");
    close(to_engine[0]);
    close(to_engine[1]);
    return false;
  }

  child_pid_ = fork();

  if (child_pid_ < 0) {
    perror("[Stockfish] fork() failed");
    return false;
  }

  if (child_pid_ == 0) {
    // Child process — become Stockfish
    // Ask kernel to deliver SIGTERM if parent dies
    prctl(PR_SET_PDEATHSIG, SIGTERM);

    close(to_engine[1]);    // Close write end of stdin pipe
    close(from_engine[0]);  // Close read end of stdout pipe

    dup2(to_engine[0], STDIN_FILENO);
    dup2(from_engine[1], STDOUT_FILENO);
    dup2(from_engine[1], STDERR_FILENO);

    close(to_engine[0]);
    close(from_engine[1]);

    execlp("nice", "nice", "-n", "19", actual_path.c_str(), nullptr);

    // If execlp returns, it failed
    perror("[Stockfish] execlp failed");
    _exit(1);
  }

  // Parent process
  close(to_engine[0]);    // Close read end
  close(from_engine[1]);  // Close write end

  to_engine_fd_   = to_engine[1];
  from_engine_fd_ = from_engine[0];

  // Initialize UCI protocol
  send_command("uci");
  std::string response = wait_for("uciok", 5000);

  if (response.empty()) {
    fprintf(stderr, "[Stockfish] Engine did not respond with 'uciok'\n");
    stop();
    return false;
  }

  // Read the id lines for engine info
  printf("[Stockfish] Engine started successfully.\n");

  // Force minimum CPU usage (1 thread, lowest hash, so it doesn't freeze dual-cores)
  send_command("setoption name Threads value 1");
  send_command("setoption name Hash value 16");

  // Send isready to ensure engine is fully initialized
  send_command("isready");
  wait_for("readyok", 5000);

  return true;
}

void StockfishEngine::stop()
{
  if (child_pid_ > 0) {
    send_command("quit");

    // Give it a moment to exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int   status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);

    if (result == 0) {
      // Still running, force kill
      kill(child_pid_, SIGTERM);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      waitpid(child_pid_, &status, WNOHANG);
    }

    child_pid_ = -1;
  }

  if (to_engine_fd_ >= 0) {
    close(to_engine_fd_);
    to_engine_fd_ = -1;
  }

  if (from_engine_fd_ >= 0) {
    close(from_engine_fd_);
    from_engine_fd_ = -1;
  }
}

bool StockfishEngine::is_running() const { return child_pid_ > 0; }

bool StockfishEngine::send_command(const std::string& cmd)
{
  if (to_engine_fd_ < 0)
    return false;

  // Check if child is still alive
  if (child_pid_ > 0) {
    int   status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);
    if (result != 0) {
      fprintf(stderr, "[Stockfish] Engine process died unexpectedly");
      if (result > 0 && WIFEXITED(status))
        fprintf(stderr, " (exit code: %d)", WEXITSTATUS(status));
      else if (result > 0 && WIFSIGNALED(status))
        fprintf(stderr, " (killed by signal: %d)", WTERMSIG(status));
      fprintf(stderr, ". Attempting restart...\n");

      child_pid_ = -1;
      close(to_engine_fd_);
      to_engine_fd_ = -1;
      close(from_engine_fd_);
      from_engine_fd_ = -1;

      if (!start(engine_path_)) {
        fprintf(stderr, "[Stockfish] Failed to restart engine.\n");
        return false;
      }
    }
  }

  std::string line    = cmd + "\n";
  ssize_t     written = write(to_engine_fd_, line.c_str(), line.size());

  if (written != (ssize_t) line.size()) {
    perror("[Stockfish] Failed to send command");
    return false;
  }

  return true;
}

std::string StockfishEngine::wait_for(const std::string& prefix, int timeout_ms)
{
  auto start = std::chrono::steady_clock::now();

  while (true) {
    std::string line;

    // First, try to get a full line from the buffer
    size_t pos = read_buffer_.find('\n');
    if (pos != std::string::npos) {
      line = read_buffer_.substr(0, pos);
      read_buffer_.erase(0, pos + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
    }
    else {
      // Buffer doesn't have a full line, wait for more data
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start
      )
                       .count();
      if (elapsed >= timeout_ms)
        return "";

      int remaining = timeout_ms - (int) elapsed;
      if (remaining <= 0)
        return "";

      struct pollfd pfd;
      pfd.fd      = from_engine_fd_;
      pfd.events  = POLLIN;
      pfd.revents = 0;

      int ret = poll(&pfd, 1, remaining);
      if (ret <= 0)
        return "";

      std::vector<char> buf(4096);
      int               res = read(from_engine_fd_, buf.data(), buf.size());
      if (res <= 0)
        return "";

      read_buffer_.append(buf.data(), res);
      continue;  // Go back to the top of the loop to check for \n again
    }

    if (line.empty())
      continue;

    // Capture engine identification ("id name Stockfish ...")
    if (line.size() > 8 && line.compare(0, 8, "id name ") == 0) {
      engine_info_ = line.substr(8);
    }

    // Parse info lines during search
    if (line.size() >= 4 && line.compare(0, 4, "info") == 0) {
      parse_info_line(line);
    }

    // Check if this is the line we're waiting for
    if (line.size() >= prefix.size() && line.compare(0, prefix.size(), prefix) == 0) {
      return line;
    }
  }
}

bool StockfishEngine::set_position(const std::string& fen)
{
  // Stop any ongoing search before setting a new position
  send_command("stop");
  send_command("isready");
  wait_for("readyok", 5000);

  std::string cmd = "position fen " + fen;
  if (!send_command(cmd))
    return false;

  // Sync to ensure position is set before any go command
  send_command("isready");
  wait_for("readyok", 5000);
  return true;
}

std::future<std::string> StockfishEngine::get_best_move_async(int depth)
{
  return std::async(std::launch::async, [this, depth]() { return this->get_best_move(depth); });
}

void StockfishEngine::stop_search() { send_command("stop"); }

std::string StockfishEngine::get_best_move(int depth)
{
  last_score_ = 0;
  last_pv_.clear();

  std::string cmd = "go depth " + std::to_string(depth);
  send_command(cmd);

  std::string response = wait_for("bestmove", 60000);  // 60s timeout for deep search

  if (response.empty()) {
    // Timeout reached. Tell Stockfish to stop searching and return best move so far.
    send_command("stop");
    response = wait_for("bestmove", 5000);

    // If it STILL returns empty, the engine is completely dead or stuck.
    if (response.empty())
      return "";
  }

  // Parse "bestmove e2e4 ponder e7e5"
  std::istringstream iss(response);
  std::string        token, move;

  iss >> token;  // "bestmove"
  iss >> move;   // "e2e4"

  return move;
}

bool StockfishEngine::set_option(const std::string& name, const std::string& value)
{
  std::string cmd = "setoption name " + name + " value " + value;
  return send_command(cmd);
}

bool StockfishEngine::set_threads(int n) { return set_option("Threads", std::to_string(n)); }

bool StockfishEngine::set_hash(int mb) { return set_option("Hash", std::to_string(mb)); }

std::string StockfishEngine::get_engine_info() const { return engine_info_; }

int StockfishEngine::get_last_score() const { return last_score_; }

std::string StockfishEngine::get_last_pv() const { return last_pv_; }

void StockfishEngine::parse_info_line(const std::string& line)
{
  std::istringstream iss(line);
  std::string        token;

  while (iss >> token) {
    if (token == "score") {
      std::string score_type;
      iss >> score_type;

      if (score_type == "cp") {
        iss >> last_score_;
      }
      else if (score_type == "mate") {
        int mate_in;
        iss >> mate_in;
        // Convert mate score to large centipawn value
        last_score_ = (mate_in > 0) ? 100000 - mate_in : -100000 - mate_in;
      }
    }
    else if (token == "pv") {
      // Rest of the line is the principal variation
      std::getline(iss, last_pv_);
      if (!last_pv_.empty() && last_pv_[0] == ' ')
        last_pv_ = last_pv_.substr(1);
    }
  }
}
