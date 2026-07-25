#include "mouse.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <random>
#include <thread>
#include <unistd.h>

static std::mt19937& get_rng()
{
  static std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
  return rng;
}

VirtualMouse::VirtualMouse() :
    fd_(-1),
    click_delay_min_ms_(30),
    click_delay_max_ms_(80),
    move_delay_min_ms_(5),
    move_delay_max_ms_(15),
    jitter_pixels_(2)
{
}

VirtualMouse::~VirtualMouse() { close(); }

bool VirtualMouse::open(int screen_width, int screen_height)
{
  if (fd_ >= 0)
    return true;

  fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd_ < 0) {
    fprintf(stderr, "[Mouse] Failed to open /dev/uinput (requires root/sudo)\n");
    return false;
  }

  ioctl(fd_, UI_SET_EVBIT, EV_KEY);
  ioctl(fd_, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(fd_, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(fd_, UI_SET_KEYBIT, BTN_MIDDLE);

  ioctl(fd_, UI_SET_EVBIT, EV_ABS);
  ioctl(fd_, UI_SET_ABSBIT, ABS_X);
  ioctl(fd_, UI_SET_ABSBIT, ABS_Y);

  struct uinput_user_dev uidev;
  memset(&uidev, 0, sizeof(uidev));
  strncpy(uidev.name, "ChessyNotCheesy Pointer", UINPUT_MAX_NAME_SIZE);
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor  = 0x1234;
  uidev.id.product = 0x5678;
  uidev.id.version = 1;

  uidev.absmin[ABS_X] = 0;
  uidev.absmax[ABS_X] = screen_width > 0 ? screen_width : 1920;
  uidev.absmin[ABS_Y] = 0;
  uidev.absmax[ABS_Y] = screen_height > 0 ? screen_height : 1080;

  write(fd_, &uidev, sizeof(uidev));
  ioctl(fd_, UI_DEV_CREATE);

  // Give udev some time to create the input device
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  printf("[Mouse] Virtual Pointer initialized successfully.\n");
  return true;
}

void VirtualMouse::close()
{
  if (fd_ >= 0) {
    ioctl(fd_, UI_DEV_DESTROY);
    ::close(fd_);
    fd_ = -1;
  }
}

int VirtualMouse::random_range(int min_val, int max_val)
{
  if (min_val >= max_val)
    return min_val;
  std::uniform_int_distribution<int> dist(min_val, max_val);
  return dist(get_rng());
}

void VirtualMouse::random_delay(int min_ms, int max_ms)
{
  int ms = random_range(min_ms, max_ms);
  if (ms > 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void VirtualMouse::emit(int type, int code, int val)
{
  if (fd_ < 0)
    return;
  struct input_event ie;
  memset(&ie, 0, sizeof(ie));
  ie.type  = type;
  ie.code  = code;
  ie.value = val;
  write(fd_, &ie, sizeof(ie));
}

bool VirtualMouse::move_to(int x, int y)
{
  if (fd_ < 0)
    return false;

  // Add small random jitter for human-like behavior
  if (jitter_pixels_ > 0) {
    x += random_range(-jitter_pixels_, jitter_pixels_);
    y += random_range(-jitter_pixels_, jitter_pixels_);
  }

  emit(EV_ABS, ABS_X, x);
  emit(EV_ABS, ABS_Y, y);
  emit(EV_SYN, SYN_REPORT, 0);

  return true;
}

bool VirtualMouse::click(int x, int y)
{
  if (!move_to(x, y))
    return false;

  random_delay(move_delay_min_ms_, move_delay_max_ms_);

  if (!button_press(BTN_LEFT))
    return false;

  random_delay(click_delay_min_ms_, click_delay_max_ms_);

  return button_release(BTN_LEFT);
}

bool VirtualMouse::drag(int from_x, int from_y, int to_x, int to_y)
{
  if (!move_to(from_x, from_y))
    return false;

  random_delay(50, 100);

  if (!button_press(BTN_LEFT))
    return false;

  random_delay(50, 80);

  // Interpolate the movement
  int dx    = to_x - from_x;
  int dy    = to_y - from_y;
  int steps = 20;

  for (int i = 1; i <= steps; ++i) {
    int cur_x = from_x + (dx * i) / steps;
    int cur_y = from_y + (dy * i) / steps;

    move_to(cur_x, cur_y);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Ensure we reach the exact destination
  move_to(to_x, to_y);
  random_delay(100, 150);

  if (!button_release(BTN_LEFT))
    return false;

  return true;
}

bool VirtualMouse::button_press(uint32_t button_code)
{
  if (fd_ < 0)
    return false;
  emit(EV_KEY, button_code, 1);
  emit(EV_SYN, SYN_REPORT, 0);
  return true;
}

bool VirtualMouse::button_release(uint32_t button_code)
{
  if (fd_ < 0)
    return false;
  emit(EV_KEY, button_code, 0);
  emit(EV_SYN, SYN_REPORT, 0);
  return true;
}

void VirtualMouse::set_click_delay_ms(int min_ms, int max_ms)
{
  click_delay_min_ms_ = min_ms;
  click_delay_max_ms_ = max_ms;
}

void VirtualMouse::set_move_delay_ms(int min_ms, int max_ms)
{
  move_delay_min_ms_ = min_ms;
  move_delay_max_ms_ = max_ms;
}

void VirtualMouse::set_jitter_pixels(int max_jitter) { jitter_pixels_ = max_jitter; }
