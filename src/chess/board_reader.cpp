#include "board_reader.h"

#include <algorithm>
#include <cstdio>
#include <iostream>

BoardReader::BoardReader(ScreenCapture& capture, ThemeManager& theme_manager) :
    capture_(capture),
    theme_manager_(theme_manager),
    is_calibrated_(false),
    playing_white_(true),
    board_tl_x_(0),
    board_tl_y_(0),
    board_br_x_(0),
    board_br_y_(0),
    square_size_(0)
{
}

cv::Mat BoardReader::capture_to_mat()
{
  const uint8_t* buffer = capture_.get_buffer();
  if (!buffer)
    return cv::Mat();

  // ScreenCapture returns BGRA format pixels
  cv::Mat img(
    capture_.get_capture_height(), capture_.get_capture_width(), CV_8UC4, (void*) buffer,
    capture_.get_bytes_per_line()
  );

  // OpenCV matchTemplate often works better with BGR or grayscale
  cv::Mat bgr;
  cv::cvtColor(img, bgr, cv::COLOR_BGRA2BGR, 3);
  return bgr;
}

void BoardReader::calibrate(int top_left_x, int top_left_y, int bottom_right_x, int bottom_right_y)
{
  board_tl_x_ = std::min(top_left_x, bottom_right_x);
  board_tl_y_ = std::min(top_left_y, bottom_right_y);
  board_br_x_ = std::max(top_left_x, bottom_right_x);
  board_br_y_ = std::max(top_left_y, bottom_right_y);

  int board_w = board_br_x_ - board_tl_x_;
  int sq_size = board_w / 8;

  if (sq_size < 10) {
    std::cerr << "[BoardReader] Invalid calibration: board too small.\n";
    return;
  }

  square_size_   = sq_size;
  is_calibrated_ = true;

  printf(
    "[BoardReader] Calibrated: board at (%d, %d), "
    "square size %d\n",
    board_tl_x_, board_tl_y_, square_size_
  );
}

void BoardReader::set_playing_white(bool white) 
{ 
  playing_white_ = white; 
  reset_cache();
}

void BoardReader::reset_cache()
{
  prev_screen_ = cv::Mat();
}
bool BoardReader::is_playing_white() const { return playing_white_; }

bool BoardReader::is_calibrated() const { return is_calibrated_; }

int BoardReader::get_square_size() const { return square_size_; }

void BoardReader::get_square_center(int file, int rank, int& screen_x, int& screen_y) const
{
  // file: 0=a, 7=h — rank: 0=1, 7=8
  // If playing white: a8 is at top-left
  int screen_file, screen_rank;

  if (playing_white_) {
    screen_file = file;
    screen_rank = 7 - rank;
  }
  else {
    screen_file = 7 - file;
    screen_rank = rank;
  }

  screen_x = board_tl_x_ + (screen_file * square_size_) + (square_size_ / 2);
  screen_y = board_tl_y_ + (screen_rank * square_size_) + (square_size_ / 2);
}

Board BoardReader::read_board()
{
  Board board = {};
  if (!is_calibrated_)
    return board;

  // Increase margin to 16 to allow for up to 16 pixels of manual calibration error or window shift
  int margin = 16;
  if (!capture_.capture_region(
        board_tl_x_ - margin, board_tl_y_ - margin, (square_size_ * 8) + (margin * 2),
        (square_size_ * 8) + (margin * 2)
      ))
    return board;
  cv::Mat screen = capture_to_mat();
  if (screen.empty())
    return board;

  if (cached_square_size_ != square_size_) {
    precomputed_.clear();
    for (int p = 1; p <= 12; p++) {
      cv::Mat templ = theme_manager_.get_template(p);
      cv::Mat mask  = theme_manager_.get_mask(p);

      if (templ.empty())
        continue;

      cv::Mat search_templ = templ;
      cv::Mat search_mask  = mask;

      if (!search_mask.empty()) {
        cv::threshold(search_mask, search_mask, 240, 255, cv::THRESH_BINARY);
      }

      if (search_templ.cols != square_size_ || search_templ.rows != square_size_) {
        cv::resize(
          search_templ, search_templ, cv::Size(square_size_, square_size_), 0, 0, cv::INTER_LINEAR
        );
        if (!search_mask.empty()) {
          cv::resize(
            search_mask, search_mask, cv::Size(square_size_, square_size_), 0, 0, cv::INTER_NEAREST
          );
        }
      }

      if (!search_mask.empty()) {
        cv::erode(search_mask, search_mask, cv::Mat(), cv::Point(-1, -1), 2);
      }

      PrecomputedTemplate pt;
      cv::GaussianBlur(search_templ, pt.templ_blur, cv::Size(3, 3), 0);
      if (!search_mask.empty()) {
        cv::cvtColor(search_mask, pt.search_mask_3c, cv::COLOR_GRAY2BGR);
      }
      precomputed_[p] = pt;
    }
    cached_square_size_ = square_size_;
  }

  // For every square, we crop it and match against all piece templates
  bool has_prev = !prev_screen_.empty() && prev_screen_.size() == screen.size();
  for (int rank = 0; rank < 8; rank++) {
    for (int file = 0; file < 8; file++) {
      int screen_file = playing_white_ ? file : (7 - file);
      int screen_rank = playing_white_ ? (7 - rank) : rank;

      int sx = (screen_file * square_size_) + margin;
      int sy = (screen_rank * square_size_) + margin;

      int roi_x = std::max(0, sx - margin);
      int roi_y = std::max(0, sy - margin);
      int roi_w = square_size_ + (margin * 2);
      int roi_h = square_size_ + (margin * 2);

      roi_w = std::min(roi_w, screen.cols - roi_x);
      roi_h = std::min(roi_h, screen.rows - roi_y);

      cv::Rect roi(roi_x, roi_y, roi_w, roi_h);
      cv::Mat  square = screen(roi);

      if (has_prev) {
        cv::Mat diff;
        cv::absdiff(square, prev_screen_(roi), diff);
        cv::cvtColor(diff, diff, cv::COLOR_BGR2GRAY);
        int changed_pixels = cv::countNonZero(diff > 5);
        // If less than 30 pixels changed, skip heavy template matching (reduces CPU usage from idle animations or screen noise)
        if (changed_pixels < 30) {
          board[rank][file] = prev_board_[rank][file];
          continue;
        }
      }

      cv::Mat square_blur;
      cv::GaussianBlur(square, square_blur, cv::Size(3, 3), 0);

      double best_match = 1e9;  // SQDIFF is lower-is-better
      Piece  best_piece = Piece::EMPTY;

      for (int p = 1; p <= 12; p++) {
        if (precomputed_.find(p) == precomputed_.end())
          continue;

        const PrecomputedTemplate& pt = precomputed_[p];

        if (square_blur.cols < pt.templ_blur.cols || square_blur.rows < pt.templ_blur.rows) {
          continue;
        }

        cv::Mat result;
        if (!pt.search_mask_3c.empty()) {
          cv::matchTemplate(
            square_blur, pt.templ_blur, result, cv::TM_SQDIFF_NORMED, pt.search_mask_3c
          );
        }
        else {
          cv::matchTemplate(square_blur, pt.templ_blur, result, cv::TM_SQDIFF_NORMED);
        }

        double minVal, maxVal;
        cv::minMaxLoc(result, &minVal, &maxVal);

        if (minVal < best_match) {
          best_match = minVal;
          best_piece = static_cast<Piece>(p);
        }
      }

      // If the best matching piece still has a difference > 0.08,
      // it means this square is actually empty and the match was a hallucination.
      // True matches typically score < 0.01. Background hallucinations score > 0.14.
      if (best_match > 0.08) {
        best_piece = Piece::EMPTY;
      }

      if (rank == 0 && file == 0) {
        printf(
          "[BoardReader] Debug: square (0,0) ROI=(%d,%d %dx%d) best_match = %f, best_piece = %d\n",
          roi_x, roi_y, roi_w, roi_h, best_match, (int) best_piece
        );
      }

      board[rank][file] = best_piece;
    }
  }

  prev_screen_ = screen.clone();
  prev_board_  = board;

  // Debug: Save the full board region that we are analyzing
  return board;
}

char BoardReader::piece_to_fen(Piece p)
{
  switch (p) {
  case Piece::WHITE_PAWN :
    return 'P';
  case Piece::WHITE_KNIGHT :
    return 'N';
  case Piece::WHITE_BISHOP :
    return 'B';
  case Piece::WHITE_ROOK :
    return 'R';
  case Piece::WHITE_QUEEN :
    return 'Q';
  case Piece::WHITE_KING :
    return 'K';
  case Piece::BLACK_PAWN :
    return 'p';
  case Piece::BLACK_KNIGHT :
    return 'n';
  case Piece::BLACK_BISHOP :
    return 'b';
  case Piece::BLACK_ROOK :
    return 'r';
  case Piece::BLACK_QUEEN :
    return 'q';
  case Piece::BLACK_KING :
    return 'k';
  default :
    return '.';
  }
}
