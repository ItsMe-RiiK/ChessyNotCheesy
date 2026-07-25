#ifndef CHESSY_NOT_CHEESY_SCREEN_CAPTURE_H
#define CHESSY_NOT_CHEESY_SCREEN_CAPTURE_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

// X11 defines macros that conflict with standard C++ and OpenCV
#undef Status
#undef Success
#undef None
#undef Bool
#undef True
#undef False
#include <cstdint>
#include <string>
#include <sys/shm.h>
#include <vector>

/*
 * ScreenCapture — Hybrid X11 / Wayland screen capture
 *
 * Captures screen regions at high speed.
 * Uses XShm for X11 sessions, and `grim` for Wayland sessions.
 */

struct Pixel
{
  uint8_t r, g, b;
};

class ScreenCapture
{
public:
  ScreenCapture();
  ~ScreenCapture();

  // Initialize display connection
  bool init();
  void cleanup();

  // Capture a region of the screen into the internal buffer
  // Returns pointer to pixel data (BGRA format, row-major)
  bool capture_region(int x, int y, int width, int height);

  // Get raw buffer from last capture
  const uint8_t* get_buffer() const;
  int            get_capture_width() const;
  int            get_capture_height() const;

  // Screen dimensions
  int get_screen_width() const;
  int get_screen_height() const;
  int get_bytes_per_line() const;

  bool is_wayland() const;

private:
  bool is_wayland_;

  // Generic buffer used for Wayland (grim outputs to /dev/shm)
  std::vector<uint8_t> wayland_buffer_;
  std::vector<uint8_t> wayland_rgb_buffer_;

  // X11 properties
  Display* display_;
  Window   root_;
  int      screen_width_;
  int      screen_height_;

  // XShm resources
  XImage*         ximage_;
  XShmSegmentInfo shm_info_;
  bool            shm_attached_;

  // Current capture dimensions
  int cap_x_, cap_y_;
  int cap_width_, cap_height_;

  void free_shm();
  bool alloc_shm(int width, int height);

  bool capture_region_wayland(int x, int y, int width, int height);
};

#endif /* CHESSY_NOT_CHEESY_SCREEN_CAPTURE_H */
