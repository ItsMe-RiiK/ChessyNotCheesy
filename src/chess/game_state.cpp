#include "game_state.h"

#include <cstdio>
#include <cstring>

GameState::GameState() :
    board_changed_(false),
    playing_white_(true),
    white_to_move_(true),
    halfmove_clock_(0),
    fullmove_number_(1),
    white_castle_king_(true),
    white_castle_queen_(true),
    black_castle_king_(true),
    black_castle_queen_(true)
{
  setup_initial_board();
  prev_board_ = board_;
}

void GameState::reset()
{
  setup_initial_board();
  prev_board_    = board_;
  board_changed_ = false;
  white_to_move_ = true;
  move_history_.clear();
  last_move_.clear();
  halfmove_clock_     = 0;
  fullmove_number_    = 1;
  white_castle_king_  = true;
  white_castle_queen_ = true;
  black_castle_king_  = true;
  black_castle_queen_ = true;
  en_passant_square_.clear();
}

void GameState::setup_initial_board()
{
  // Clear board
  for (auto& row : board_)
    row.fill(Piece::EMPTY);

  // White pieces (rank 0 = rank 1)
  board_[0][0] = Piece::WHITE_ROOK;
  board_[0][1] = Piece::WHITE_KNIGHT;
  board_[0][2] = Piece::WHITE_BISHOP;
  board_[0][3] = Piece::WHITE_QUEEN;
  board_[0][4] = Piece::WHITE_KING;
  board_[0][5] = Piece::WHITE_BISHOP;
  board_[0][6] = Piece::WHITE_KNIGHT;
  board_[0][7] = Piece::WHITE_ROOK;

  // White pawns (rank 1 = rank 2)
  for (int f = 0; f < 8; f++)
    board_[1][f] = Piece::WHITE_PAWN;

  // Black pawns (rank 6 = rank 7)
  for (int f = 0; f < 8; f++)
    board_[6][f] = Piece::BLACK_PAWN;

  // Black pieces (rank 7 = rank 8)
  board_[7][0] = Piece::BLACK_ROOK;
  board_[7][1] = Piece::BLACK_KNIGHT;
  board_[7][2] = Piece::BLACK_BISHOP;
  board_[7][3] = Piece::BLACK_QUEEN;
  board_[7][4] = Piece::BLACK_KING;
  board_[7][5] = Piece::BLACK_BISHOP;
  board_[7][6] = Piece::BLACK_KNIGHT;
  board_[7][7] = Piece::BLACK_ROOK;
}

void GameState::set_playing_white(bool white) { playing_white_ = white; }

bool GameState::is_playing_white() const { return playing_white_; }

bool GameState::update(const Board& detected_board)
{
  bool changed = false;
  for (int r = 0; r < 8; r++) {
    for (int f = 0; f < 8; f++) {
      // Check if occupancy changed compared to our internal board state
      bool was_empty = (board_[r][f] == Piece::EMPTY);
      bool now_empty = (detected_board[r][f] == Piece::EMPTY);

      bool was_white =
        (!was_empty && board_[r][f] >= Piece::WHITE_PAWN && board_[r][f] <= Piece::WHITE_KING);
      bool now_white =
        (detected_board[r][f] >= Piece::WHITE_PAWN && detected_board[r][f] <= Piece::WHITE_KING);

      if (was_empty != now_empty || (!was_empty && !now_empty && was_white != now_white)) {
        changed = true;
        break;
      }
    }
    if (changed)
      break;
  }

  if (changed) {
    // Detect what move was made
    last_move_ = detect_move(board_, detected_board);

    if (!last_move_.empty()) {
      // Apply the move to our tracked board
      apply_move(last_move_);

      printf("[GameState] Move detected: %s (FEN: %s)\n", last_move_.c_str(), to_fen().c_str());

      board_changed_ = true;
    }
    else {
      // Could not isolate a single move
      printf(
        "[GameState] ERROR: Move detection failed! The board changed, but no valid move was found.\n"
      );
      printf("[GameState] Emptied or filled squares were ambiguous.\n");
      // Do not update board_, we will try again next tick
      board_changed_ = false;
    }
  }
  else {
    board_changed_ = false;
  }

  return board_changed_;
}

std::string GameState::detect_move(const Board& old_board, const Board& new_board)
{
  // Find squares that changed
  std::vector<std::pair<int, int>> emptied;  // squares that became empty
  std::vector<std::pair<int, int>> filled;   // squares that became occupied or changed piece
  std::vector<std::pair<int, int>> changed;  // squares where piece color changed

  auto check_promotion =
    [&](const std::string& move, int src_x, int src_y, int dst_x, int dst_y) -> std::string {
    if (move.empty())
      return move;
    Piece p = old_board[src_y][src_x];
    if ((p == Piece::WHITE_PAWN && dst_y == 0) || (p == Piece::BLACK_PAWN && dst_y == 7)) {
      Piece promoted = new_board[dst_y][dst_x];
      if (promoted == Piece::WHITE_QUEEN || promoted == Piece::BLACK_QUEEN)
        return move + "q";
      if (promoted == Piece::WHITE_ROOK || promoted == Piece::BLACK_ROOK)
        return move + "r";
      if (promoted == Piece::WHITE_BISHOP || promoted == Piece::BLACK_BISHOP)
        return move + "b";
      if (promoted == Piece::WHITE_KNIGHT || promoted == Piece::BLACK_KNIGHT)
        return move + "n";
      return move + "q";  // fallback to queen if unknown
    }
    return move;
  };

  for (int r = 0; r < 8; r++) {
    for (int f = 0; f < 8; f++) {
      bool old_empty = (old_board[r][f] == Piece::EMPTY);
      bool new_empty = (new_board[r][f] == Piece::EMPTY);

      if (!old_empty && new_empty) {
        emptied.push_back({f, r});
      }
      else if (old_empty && !new_empty) {
        filled.push_back({f, r});
      }
      else if (!old_empty && !new_empty) {
        // Both occupied — check if piece color changed (capture + new piece)
        bool old_white =
          (old_board[r][f] >= Piece::WHITE_PAWN && old_board[r][f] <= Piece::WHITE_KING);
        bool new_white =
          (new_board[r][f] >= Piece::WHITE_PAWN && new_board[r][f] <= Piece::WHITE_KING);
        if (old_white != new_white) {
          changed.push_back({f, r});
        }
      }
    }
  }

  // DEBUG PRINT
  if (!emptied.empty() || !filled.empty() || !changed.empty()) {
    printf(
      "[GameState] Debug: emptied=%zu, filled=%zu, changed=%zu\n", emptied.size(), filled.size(),
      changed.size()
    );
    for (auto& sq : emptied)
      printf("  Emptied: %s\n", square_to_uci(sq.first, sq.second).c_str());
    for (auto& sq : filled)
      printf("  Filled: %s\n", square_to_uci(sq.first, sq.second).c_str());
    for (auto& sq : changed)
      printf("  Changed: %s\n", square_to_uci(sq.first, sq.second).c_str());
  }

  // Simple move: one square emptied, one filled
  if (emptied.size() == 1 && filled.size() == 1 && changed.empty()) {
    return check_promotion(
      square_to_uci(emptied[0].first, emptied[0].second)
        + square_to_uci(filled[0].first, filled[0].second),
      emptied[0].first, emptied[0].second, filled[0].first, filled[0].second
    );
  }

  // Capture: one emptied, one changed
  if (emptied.size() == 1 && filled.empty() && changed.size() == 1) {
    return check_promotion(
      square_to_uci(emptied[0].first, emptied[0].second)
        + square_to_uci(changed[0].first, changed[0].second),
      emptied[0].first, emptied[0].second, changed[0].first, changed[0].second
    );
  }

  // Castling: (Full animation) two emptied (king + rook), two filled (king + rook new positions)
  // Or (Mid animation): King emptied, King filled/changed, Rook emptied/filled partially
  if (emptied.size() >= 1) {
    for (auto& e : emptied) {
      Piece p = old_board[e.second][e.first];
      if (p == Piece::WHITE_KING || p == Piece::BLACK_KING) {
        // Check if the King moved 2 squares (Castling)
        for (auto& fl : filled) {
          if (fl.second == e.second && std::abs(fl.first - e.first) == 2)
            return square_to_uci(e.first, e.second) + square_to_uci(fl.first, fl.second);
        }
        for (auto& ch : changed) {
          if (ch.second == e.second && std::abs(ch.first - e.first) == 2)
            return square_to_uci(e.first, e.second) + square_to_uci(ch.first, ch.second);
        }

        // If it's a full 2-2 castling but the king didn't move 2 squares (impossible logically, but as a fallback for standard King moves)
        if (emptied.size() == 2 && filled.size() == 2) {
          for (auto& fl : filled) {
            if (fl.second == e.second || std::abs(fl.second - e.second) <= 1)
              return square_to_uci(e.first, e.second) + square_to_uci(fl.first, fl.second);
          }
        }
      }
    }

    if (emptied.size() == 2 && filled.size() == 2) {
      // Fallback: first emptied → first filled
      return square_to_uci(emptied[0].first, emptied[0].second)
           + square_to_uci(filled[0].first, filled[0].second);
    }
  }

  // En passant: two emptied (pawn + captured pawn), one filled
  if (emptied.size() == 2 && filled.size() == 1) {
    // The moving piece is the one whose file matches the destination
    for (auto& e : emptied) {
      if (std::abs(e.first - filled[0].first) <= 1) {
        Piece p = old_board[e.second][e.first];
        if (p == Piece::WHITE_PAWN || p == Piece::BLACK_PAWN) {
          return square_to_uci(e.first, e.second)
               + square_to_uci(filled[0].first, filled[0].second);
        }
      }
    }
  }

  // Fallback: if we can identify any move, but only if it's reasonably isolated
  if (!emptied.empty() && (!filled.empty() || !changed.empty())) {
    if (emptied.size() + filled.size() + changed.size() > 6) {
      printf(
        "[GameState] Vision unstable (too many changed squares: %zu). Ignoring frame.\n",
        emptied.size() + filled.size() + changed.size()
      );
      return "";
    }

    auto& dest = !filled.empty() ? filled[0] : changed[0];
    return check_promotion(
      square_to_uci(emptied[0].first, emptied[0].second) + square_to_uci(dest.first, dest.second),
      emptied[0].first, emptied[0].second, dest.first, dest.second
    );
  }

  return "";  // Could not detect move
}

void GameState::apply_move(const std::string& uci_move)
{
  if (uci_move.size() < 4)
    return;

  int from_file, from_rank, to_file, to_rank;
  if (!uci_to_square(uci_move.substr(0, 2), from_file, from_rank))
    return;
  if (!uci_to_square(uci_move.substr(2, 2), to_file, to_rank))
    return;

  Piece moving = board_[from_rank][from_file];
  if (moving == Piece::EMPTY) {
    printf(
      "[GameState] WARNING: Attempted to move an empty square! (%s). Ignoring.\n", uci_move.c_str()
    );
    return;
  }

  // Handle castling
  if (
    (moving == Piece::WHITE_KING || moving == Piece::BLACK_KING)
    && std::abs(to_file - from_file) == 2
  ) {
    // Move the rook too
    if (to_file > from_file) {
      // King-side
      board_[from_rank][5] = board_[from_rank][7];
      board_[from_rank][7] = Piece::EMPTY;
    }
    else {
      // Queen-side
      board_[from_rank][3] = board_[from_rank][0];
      board_[from_rank][0] = Piece::EMPTY;
    }
  }

  // Handle en passant capture
  if (
    (moving == Piece::WHITE_PAWN || moving == Piece::BLACK_PAWN) && from_file != to_file
    && board_[to_rank][to_file] == Piece::EMPTY
  ) {
    // Remove the captured pawn
    board_[from_rank][to_file] = Piece::EMPTY;
  }

  // Handle pawn promotion
  if (uci_move.size() == 5) {
    char promo = uci_move[4];
    bool white = (moving == Piece::WHITE_PAWN);

    switch (promo) {
    case 'q' :
      moving = white ? Piece::WHITE_QUEEN : Piece::BLACK_QUEEN;
      break;
    case 'r' :
      moving = white ? Piece::WHITE_ROOK : Piece::BLACK_ROOK;
      break;
    case 'b' :
      moving = white ? Piece::WHITE_BISHOP : Piece::BLACK_BISHOP;
      break;
    case 'n' :
      moving = white ? Piece::WHITE_KNIGHT : Piece::BLACK_KNIGHT;
      break;
    }
  }

  // Execute the move
  board_[to_rank][to_file]     = moving;
  board_[from_rank][from_file] = Piece::EMPTY;

  // Update en passant
  en_passant_square_.clear();
  if (
    (moving == Piece::WHITE_PAWN && from_rank == 1 && to_rank == 3)
    || (moving == Piece::BLACK_PAWN && from_rank == 6 && to_rank == 4)
  ) {
    int ep_rank        = (from_rank + to_rank) / 2;
    en_passant_square_ = square_to_uci(to_file, ep_rank);
  }

  // Update castling rights
  update_castling(uci_move);

  if (
    moving == Piece::WHITE_PAWN || moving == Piece::BLACK_PAWN
    || board_[to_rank][to_file] != Piece::EMPTY
  ) {
    halfmove_clock_ = 0;
  }
  else {
    halfmove_clock_++;
  }

  // Toggle turn
  white_to_move_ = !white_to_move_;
  if (white_to_move_)
    fullmove_number_++;

  move_history_.push_back(uci_move);
}

void GameState::update_castling(const std::string& move)
{
  int from_file, from_rank;
  if (!uci_to_square(move.substr(0, 2), from_file, from_rank))
    return;

  // King moved
  if (from_file == 4 && from_rank == 0) {
    white_castle_king_  = false;
    white_castle_queen_ = false;
  }
  if (from_file == 4 && from_rank == 7) {
    black_castle_king_  = false;
    black_castle_queen_ = false;
  }

  // Rook moved or captured
  if (from_file == 0 && from_rank == 0)
    white_castle_queen_ = false;
  if (from_file == 7 && from_rank == 0)
    white_castle_king_ = false;
  if (from_file == 0 && from_rank == 7)
    black_castle_queen_ = false;
  if (from_file == 7 && from_rank == 7)
    black_castle_king_ = false;
}

std::string GameState::to_fen() const
{
  std::string fen;

  // Piece placement (from rank 8 to rank 1)
  for (int rank = 7; rank >= 0; rank--) {
    int empty_count = 0;

    for (int file = 0; file < 8; file++) {
      if (board_[rank][file] == Piece::EMPTY) {
        empty_count++;
      }
      else {
        if (empty_count > 0) {
          fen += std::to_string(empty_count);
          empty_count = 0;
        }
        fen += BoardReader::piece_to_fen(board_[rank][file]);
      }
    }

    if (empty_count > 0)
      fen += std::to_string(empty_count);

    if (rank > 0)
      fen += '/';
  }

  // Active color
  fen += white_to_move_ ? " w " : " b ";

  // Castling availability
  std::string castling;
  if (white_castle_king_)
    castling += 'K';
  if (white_castle_queen_)
    castling += 'Q';
  if (black_castle_king_)
    castling += 'k';
  if (black_castle_queen_)
    castling += 'q';
  if (castling.empty())
    castling = "-";
  fen += castling;

  // En passant
  fen += " " + (en_passant_square_.empty() ? "-" : en_passant_square_);

  // Halfmove clock and fullmove number
  fen += " " + std::to_string(halfmove_clock_);
  fen += " " + std::to_string(fullmove_number_);

  return fen;
}

bool GameState::is_our_turn() const
{
  if (playing_white_)
    return white_to_move_;
  else
    return !white_to_move_;
}

std::string GameState::get_last_move() const { return last_move_; }

std::string GameState::get_move_history() const
{
  std::string result;
  for (size_t i = 0; i < move_history_.size(); i++) {
    if (i > 0)
      result += ' ';
    result += move_history_[i];
  }
  return result;
}

const Board& GameState::get_board() const { return board_; }

std::string GameState::square_to_uci(int file, int rank)
{
  std::string s;
  s += (char) ('a' + file);
  s += (char) ('1' + rank);
  return s;
}

bool GameState::uci_to_square(const std::string& uci, int& file, int& rank)
{
  if (uci.size() < 2)
    return false;

  file = uci[0] - 'a';
  rank = uci[1] - '1';

  return (file >= 0 && file < 8 && rank >= 0 && rank < 8);
}
