#include "screen_capture.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/shm.h>

ScreenCapture::ScreenCapture() :
    is_wayland_(false),
    display_(nullptr),
    root_(0),
    screen_width_(0),
    screen_height_(0),
    ximage_(nullptr),
    shm_attached_(false),
    cap_x_(0),
    cap_y_(0),
    cap_width_(0),
    cap_height_(0)
{
}

ScreenCapture::~ScreenCapture() { cleanup(); }

bool ScreenCapture::init()
{
  if (getenv("WAYLAND_DISPLAY")) {
    is_wayland_ = true;

    // Get screen dimensions using grim
    FILE* f = popen("grim -t ppm - 2>/dev/null", "r");
    if (f) {
      char header[16];
      if (fgets(header, sizeof(header), f) && strncmp(header, "P6", 2) == 0) {
        fscanf(f, "%d %d\n", &screen_width_, &screen_height_);
      }
      pclose(f);
    }

    if (screen_width_ == 0)
      screen_width_ = 1920;
    if (screen_height_ == 0)
      screen_height_ = 1080;

    printf("[Capture] Wayland initialized: %dx%d, using grim\n", screen_width_, screen_height_);
    return true;
  }

  // X11 Initialization
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    std::cerr << "[Capture] Failed to open X11 Display\n";
    return false;
  }

  int screen     = DefaultScreen(display_);
  root_          = RootWindow(display_, screen);
  screen_width_  = DisplayWidth(display_, screen);
  screen_height_ = DisplayHeight(display_, screen);

  if (!XShmQueryExtension(display_)) {
    std::cerr << "[Capture] XShm extension not available!\n";
    return false;
  }

  printf("[Capture] X11 initialized: %dx%d, XShm available\n", screen_width_, screen_height_);
  return true;
}

void ScreenCapture::cleanup()
{
  if (is_wayland_) {
    wayland_buffer_.clear();
    return;
  }

  if (display_) {
    free_shm();
    XCloseDisplay(display_);
    display_ = nullptr;
  }
}

void ScreenCapture::free_shm()
{
  if (ximage_) {
    if (shm_attached_) {
      XShmDetach(display_, &shm_info_);
      shm_attached_ = false;
    }
    XDestroyImage(ximage_);
    ximage_ = nullptr;
  }

  if (shm_info_.shmaddr) {
    shmdt(shm_info_.shmaddr);
    shm_info_.shmaddr = nullptr;
  }

  if (shm_info_.shmid != -1) {
    shmctl(shm_info_.shmid, IPC_RMID, nullptr);
    shm_info_.shmid = -1;
  }
}

bool ScreenCapture::alloc_shm(int width, int height)
{
  if (ximage_ && ximage_->width == width && ximage_->height == height)
    return true;

  free_shm();

  ximage_ = XShmCreateImage(
    display_, DefaultVisual(display_, DefaultScreen(display_)), 24, ZPixmap, nullptr, &shm_info_,
    width, height
  );

  if (!ximage_)
    return false;

  shm_info_.shmid =
    shmget(IPC_PRIVATE, ximage_->bytes_per_line * ximage_->height, IPC_CREAT | 0600);
  if (shm_info_.shmid == -1)
    return false;

  shm_info_.shmaddr  = (char*) shmat(shm_info_.shmid, nullptr, 0);
  ximage_->data      = shm_info_.shmaddr;
  shm_info_.readOnly = 0;

  if (!XShmAttach(display_, &shm_info_))
    return false;

  shm_attached_ = true;
  return true;
}

bool ScreenCapture::capture_region_wayland(int x, int y, int width, int height)
{
  static int         last_x = -1, last_y = -1, last_w = -1, last_h = -1;
  static std::string cached_cmd;

  if (x != last_x || y != last_y || width != last_w || height != last_h) {
    cached_cmd = "grim -g \"" + std::to_string(x) + "," + std::to_string(y) + " "
               + std::to_string(width) + "x" + std::to_string(height)
               + "\" -t ppm - 2>/dev/null";
    last_x     = x;
    last_y     = y;
    last_w     = width;
    last_h     = height;
  }

  FILE* f = popen(cached_cmd.c_str(), "r");
  if (!f)
    return false;

  char header[16];
  if (!fgets(header, sizeof(header), f) || strncmp(header, "P6", 2) != 0) {
    pclose(f);
    return false;
  }

  int w, h, max_val;
  if (fscanf(f, "%d %d %d", &w, &h, &max_val) != 3) {
    pclose(f);
    return false;
  }
  fgetc(f);  // Consume exactly the single whitespace (usually \n) after max_val

  wayland_buffer_.resize(w * h * 4);
  wayland_rgb_buffer_.resize(w * h * 3);

  size_t bytes_read = fread(wayland_rgb_buffer_.data(), 1, wayland_rgb_buffer_.size(), f);
  pclose(f);

  if (bytes_read < wayland_rgb_buffer_.size())
    return false;

  // Convert RGB to BGRA
  const uint8_t* rgb  = wayland_rgb_buffer_.data();
  uint8_t*       bgra = wayland_buffer_.data();
  for (int i = 0; i < w * h; i++) {
    bgra[i * 4 + 0] = rgb[i * 3 + 2];  // B
    bgra[i * 4 + 1] = rgb[i * 3 + 1];  // G
    bgra[i * 4 + 2] = rgb[i * 3 + 0];  // R
    bgra[i * 4 + 3] = 255;             // A
  }

  cap_width_  = w;
  cap_height_ = h;
  cap_x_      = x;
  cap_y_      = y;

  return true;
}

bool ScreenCapture::capture_region(int x, int y, int width, int height)
{
  if (is_wayland_) {
    return capture_region_wayland(x, y, width, height);
  }

  if (!display_)
    return false;

  // Clamp to screen bounds
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x + width > screen_width_)
    width = screen_width_ - x;
  if (y + height > screen_height_)
    height = screen_height_ - y;

  if (width <= 0 || height <= 0)
    return false;

  if (!alloc_shm(width, height))
    return false;

  cap_x_      = x;
  cap_y_      = y;
  cap_width_  = width;
  cap_height_ = height;

  if (!XShmGetImage(display_, root_, ximage_, x, y, AllPlanes))
    return false;

  return true;
}

const uint8_t* ScreenCapture::get_buffer() const
{
  if (is_wayland_) {
    return wayland_buffer_.empty() ? nullptr : wayland_buffer_.data();
  }
  return ximage_ ? (const uint8_t*) ximage_->data : nullptr;
}

int  ScreenCapture::get_capture_width() const { return cap_width_; }
int  ScreenCapture::get_capture_height() const { return cap_height_; }
int  ScreenCapture::get_screen_width() const { return screen_width_; }
int  ScreenCapture::get_screen_height() const { return screen_height_; }
bool ScreenCapture::is_wayland() const { return is_wayland_; }

int ScreenCapture::get_bytes_per_line() const
{
  if (is_wayland_) {
    return cap_width_ * 4;
  }
  return ximage_ ? ximage_->bytes_per_line : 0;
}
