#include "gui.h"

#include "../bot/bot_controller.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <linux/input.h>
#include <string>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>

/* ============================================================
 * Global state for GUI module
 * ============================================================ */

static BotController*     g_bot        = nullptr;
static std::atomic<bool>* g_bot_active = nullptr;

#define bot (*g_bot)
#define bot_active (*g_bot_active)

// GUI widgets
static GtkWidget* toggle_btn;
static GtkWidget* calibrate_btn;
static GtkWidget* status_label;
static GtkWidget* depth_scale;
static GtkWidget* delay_min_spin;
static GtkWidget* delay_max_spin;
static GtkWidget* color_combo;
static GtkWidget* cpu_combo;
static GtkWidget* cpu_spin;
static GtkWidget* memory_combo;
static GtkWidget* memory_spin;

// PGN view
static GtkTextBuffer* pgn_buffer = nullptr;
static GtkWidget*     pgn_view   = nullptr;

// Calibration state
static std::atomic<int> calib_state(0);  // 0=idle, 1=waiting_first, 2=waiting_second
static int              calib_x1, calib_y1;

// Window visibility state for hotkeys
static std::atomic<bool> g_window_visible(true);

/* ============================================================
 * GUI update helpers (thread-safe via g_idle_add)
 * ============================================================ */

struct StatusUpdate
{
  std::string text;
};

static int g_ply_count = 0;

static gboolean update_status_idle(gpointer data)
{
  StatusUpdate* upd = (StatusUpdate*) data;
  if (status_label) {
    gtk_label_set_text(GTK_LABEL(status_label), upd->text.c_str());
  }
  delete upd;
  return G_SOURCE_REMOVE;
}

static gboolean update_move_idle(gpointer data)
{
  auto* upd = static_cast<StatusUpdate*>(data);
  if (pgn_buffer) {
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(pgn_buffer, &iter);

    g_ply_count++;
    std::string text;
    char        buf[64];
    if (g_ply_count % 2 == 1) {
      // White's move
      snprintf(buf, sizeof(buf), "%3d. %-7s ", (g_ply_count + 1) / 2, upd->text.c_str());
      text = buf;
    }
    else {
      // Black's move
      snprintf(buf, sizeof(buf), "%-7s\n", upd->text.c_str());
      text = buf;
    }

    gtk_text_buffer_insert(pgn_buffer, &iter, text.c_str(), -1);

    // Scroll to bottom
    if (pgn_view) {
      GtkTextIter end_iter;
      gtk_text_buffer_get_end_iter(pgn_buffer, &end_iter);
      gtk_text_buffer_select_range(pgn_buffer, &end_iter, &end_iter);
      GtkTextMark* mark = gtk_text_buffer_get_insert(pgn_buffer);
      gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(pgn_view), mark);
    }
  }

  delete upd;
  return G_SOURCE_REMOVE;
}

static gboolean update_toggle_btn_idle(gpointer data)
{
  bool active = bot_active.load();
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_btn), active);
  return G_SOURCE_REMOVE;
}

static gboolean update_color_combo_idle(gpointer data)
{
  bool is_white = (bool) (intptr_t) data;
  gtk_combo_box_set_active(GTK_COMBO_BOX(color_combo), is_white ? 0 : 1);
  return G_SOURCE_REMOVE;
}

static gboolean reset_game_idle(gpointer data)
{
  if (g_bot) {
    if (g_ply_count == 0) {
      bot.set_status("Game is already at the starting position.");
      return G_SOURCE_REMOVE;
    }
    bot.reset_game();
    bot.set_status("Game memory reset to starting position.");
    g_ply_count = 0;
    if (pgn_buffer) {
      gtk_text_buffer_set_text(pgn_buffer, "", -1);
    }
  }
  return G_SOURCE_REMOVE;
}

static gboolean adjust_delay_idle(gpointer data)
{
  intptr_t action = (intptr_t) data;  // 1: min+, -1: min-, 2: max+, -2: max-
  if (!delay_min_spin || !delay_max_spin)
    return G_SOURCE_REMOVE;

  double min_val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(delay_min_spin));
  double max_val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(delay_max_spin));

  if (action == 1) {
    min_val += 50;
    if (min_val > max_val)
      max_val = min_val;
  }
  else if (action == -1) {
    min_val -= 50;
  }
  else if (action == 2) {
    max_val += 50;
  }
  else if (action == -2) {
    max_val -= 50;
    if (max_val < min_val)
      min_val = max_val;
  }

  gtk_spin_button_set_value(GTK_SPIN_BUTTON(delay_min_spin), min_val);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(delay_max_spin), max_val);

  return G_SOURCE_REMOVE;
}

static void on_calibrate_clicked(GtkWidget* widget, gpointer data);

static gboolean calibrate_idle(gpointer data)
{
  on_calibrate_clicked(calibrate_btn, NULL);
  return G_SOURCE_REMOVE;
}

/* ============================================================
 * GUI callbacks
 * ============================================================ */

static void on_toggle(GtkWidget* widget, gpointer data)
{
  bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  bot_active  = active;

  if (active) {
    gtk_button_set_label(GTK_BUTTON(widget), "[STOP] Stop Bot (Key: `)");
    bot.start();
  }
  else {
    gtk_button_set_label(GTK_BUTTON(widget), "[START] Start Bot (Key: `)");
    bot.stop();
  }
}

static void on_calibrate_clicked(GtkWidget* widget, gpointer data)
{
  if (calib_state == 0) {
    if (getenv("WAYLAND_DISPLAY")) {
      bot.set_status("Drag a box over the chessboard using slurp...");
      FILE* f = popen("slurp", "r");
      if (f) {
        char buf[256];
        if (fgets(buf, sizeof(buf), f)) {
          int x, y, w, h;
          if (sscanf(buf, "%d,%d %dx%d", &x, &y, &w, &h) == 4) {
            bot.calibrate(x, y, x + w, y + h);
            calib_state = 3;
          }
        }
        pclose(f);
      }
    }
    else {
      calib_state = 1;
      if (calibrate_btn)
        gtk_button_set_label(GTK_BUTTON(calibrate_btn), "Click TOP-LEFT (a8)");
      gtk_label_set_text(
        GTK_LABEL(status_label), "CALIBRATING: Click the TOP-LEFT corner of the board (a8 square)"
      );
    }
  }
  else if (calib_state == 3) {
    calib_state = 4;
    gtk_label_set_text(
      GTK_LABEL(status_label),
      "Confirm recalibration? Press 'C' or click Calibrate again to confirm."
    );
  }
  else if (calib_state == 4) {
    calib_state = 0;
    on_calibrate_clicked(widget, data);
  }
}

static void on_depth_changed(GtkRange* range, gpointer data)
{
  int depth = (int) gtk_range_get_value(range);
  bot.set_stockfish_depth(depth);
}

static void on_color_changed(GtkComboBox* combo, gpointer data)
{
  int color_idx = gtk_combo_box_get_active(combo);
  bot.set_playing_white(color_idx == 0);
}

static gboolean clear_skip_flag(gpointer data)
{
  g_object_set_data(G_OBJECT(data), "skip_next_status", GINT_TO_POINTER(0));
  return G_SOURCE_REMOVE;
}

static gint on_cpu_spin_input(GtkSpinButton* spin_button, gpointer new_value, gpointer data)
{
  const gchar* text    = gtk_entry_get_text(GTK_ENTRY(spin_button));
  double       val     = g_ascii_strtod(text, NULL);
  double       max_val = gtk_adjustment_get_upper(gtk_spin_button_get_adjustment(spin_button));
  if (val > max_val) {
    bot.set_status(
      "Warning: CPU threads limited to maximum based on your computer's specs: "
      + std::to_string((int) max_val) + " threads."
    );
    g_object_set_data(G_OBJECT(spin_button), "skip_next_status", GINT_TO_POINTER(1));
    g_timeout_add(100, clear_skip_flag, spin_button);
  }
  return FALSE;
}

static gint on_memory_spin_input(GtkSpinButton* spin_button, gpointer new_value, gpointer data)
{
  const gchar* text    = gtk_entry_get_text(GTK_ENTRY(spin_button));
  double       val     = g_ascii_strtod(text, NULL);
  double       max_val = gtk_adjustment_get_upper(gtk_spin_button_get_adjustment(spin_button));
  if (val > max_val) {
    bot.set_status(
      "Warning: RAM Hash limited to maximum based on your computer's specs (halved): "
      + std::to_string((int) max_val) + " MB"
    );
    g_object_set_data(G_OBJECT(spin_button), "skip_next_status", GINT_TO_POINTER(1));
    g_timeout_add(100, clear_skip_flag, spin_button);
  }
  return FALSE;
}

static void on_cpu_spin_changed(GtkSpinButton* spin, gpointer data);

static void on_cpu_changed(GtkComboBox* combo, gpointer data)
{
  int active = gtk_combo_box_get_active(combo);
  if (active == -1)
    return;  // Ignore programmatic unselection

  long num_procs = sysconf(_SC_NPROCESSORS_ONLN);

  if (num_procs < 1) {
    num_procs = 1;
  }

  int threads = 1;
  if (active == 1 && num_procs > 1) {
    threads = num_procs;
  }

  if (cpu_spin) {
    g_signal_handlers_block_by_func(cpu_spin, (gpointer) on_cpu_spin_changed, NULL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(cpu_spin), threads);
    g_signal_handlers_unblock_by_func(cpu_spin, (gpointer) on_cpu_spin_changed, NULL);
  }
  bot.set_engine_threads(threads);
}

static void on_memory_spin_changed(GtkSpinButton* spin, gpointer data);

static void on_memory_changed(GtkComboBox* combo, gpointer data)
{
  int active = gtk_combo_box_get_active(combo);
  if (active == -1)
    return;  // Ignore programmatic unselection

  int hash = 256;
  if (active == 0)
    hash = 256;
  else if (active == 1)
    hash = 512;
  else if (active == 2)
    hash = 1024;
  else if (active == 3)
    hash = 2048;

  if (memory_spin) {
    g_signal_handlers_block_by_func(memory_spin, (gpointer) on_memory_spin_changed, NULL);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(memory_spin), hash);
    g_signal_handlers_unblock_by_func(memory_spin, (gpointer) on_memory_spin_changed, NULL);
  }
  bot.set_engine_hash(hash);
}

static void on_cpu_spin_changed(GtkSpinButton* spin, gpointer data)
{
  int    threads = gtk_spin_button_get_value_as_int(spin);
  double max_val = gtk_adjustment_get_upper(gtk_spin_button_get_adjustment(spin));
  bot.set_engine_threads(threads);

  if (cpu_combo) {
    g_signal_handlers_block_by_func(cpu_combo, (gpointer) on_cpu_changed, NULL);
    gtk_combo_box_set_active(GTK_COMBO_BOX(cpu_combo), -1);  // Unselect to show 'Custom'
    g_signal_handlers_unblock_by_func(cpu_combo, (gpointer) on_cpu_changed, NULL);
  }

  int skip = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "skip_next_status"));
  if (skip) {
    g_object_set_data(G_OBJECT(spin), "skip_next_status", GINT_TO_POINTER(0));
  }
  else {
    bot.set_status("Custom CPU Threads set to: " + std::to_string(threads));
  }
}

static void on_memory_spin_changed(GtkSpinButton* spin, gpointer data)
{
  int    hash    = gtk_spin_button_get_value_as_int(spin);
  double max_val = gtk_adjustment_get_upper(gtk_spin_button_get_adjustment(spin));
  bot.set_engine_hash(hash);

  if (memory_combo) {
    g_signal_handlers_block_by_func(memory_combo, (gpointer) on_memory_changed, NULL);
    gtk_combo_box_set_active(GTK_COMBO_BOX(memory_combo), -1);  // Unselect to show 'Custom'
    g_signal_handlers_unblock_by_func(memory_combo, (gpointer) on_memory_changed, NULL);
  }

  int skip = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(spin), "skip_next_status"));
  if (skip) {
    g_object_set_data(G_OBJECT(spin), "skip_next_status", GINT_TO_POINTER(0));
  }
  else {
    bot.set_status("Custom RAM Hash set to: " + std::to_string(hash) + " MB");
  }
}

static void on_spin_activate(GtkWidget* widget, gpointer data)
{
  GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
  if (GTK_IS_WINDOW(toplevel)) {
    gtk_window_set_focus(GTK_WINDOW(toplevel), NULL);
  }
}

static void on_delay_changed(GtkSpinButton* spin, gpointer data)
{
  int delay_min = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(delay_min_spin));
  int delay_max = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(delay_max_spin));
  bot.set_move_delay(delay_min, delay_max);
}

static void on_always_on_top_toggled(GtkToggleButton* togglebutton, gpointer user_data)
{
  GtkWindow* window = GTK_WINDOW(user_data);
  bool       active = gtk_toggle_button_get_active(togglebutton);
  gtk_window_set_keep_above(window, active);
}

/* ============================================================
 * Hotkey + Calibration click listener thread
 * ============================================================ */

static void input_listener_thread()
{
  int epfd = epoll_create1(0);
  if (epfd < 0)
    return;

  std::vector<int> fds;
  DIR*             dir = opendir("/dev/input");
  if (dir) {
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
      std::string name = ent->d_name;
      if (name.find("event") == 0) {
        std::string path = "/dev/input/" + name;
        int         fd   = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
          unsigned long evbit[1 + EV_MAX / 8 / sizeof(long)] = {0};
          ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit);
          // Listen for both keyboard and mouse events
          if (evbit[0] & ((1UL << EV_KEY) | (1UL << EV_ABS) | (1UL << EV_REL))) {
            fds.push_back(fd);
            struct epoll_event ev;
            ev.events  = EPOLLIN;
            ev.data.fd = fd;
            epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
          }
          else {
            close(fd);
          }
        }
      }
    }
    closedir(dir);
  }

  struct epoll_event events[10];
  bool               left_shift_pressed  = false;
  bool               right_shift_pressed = false;

  while (true) {
    int n = epoll_wait(epfd, events, 10, -1);
    for (int i = 0; i < n; i++) {
      struct input_event ev;
      while (read(events[i].data.fd, &ev, sizeof(ev)) > 0) {
        if (!g_window_visible.load())
          continue;

        if (ev.type == EV_KEY) {
          if (ev.code == KEY_LEFTSHIFT)
            left_shift_pressed = (ev.value != 0);
          if (ev.code == KEY_RIGHTSHIFT)
            right_shift_pressed = (ev.value != 0);

          if (ev.value == 1 || ev.value == 2)  // pressed or repeat
          {
            if (ev.code == KEY_EQUAL || ev.code == KEY_KPPLUS) {
              if (left_shift_pressed)
                g_idle_add(adjust_delay_idle, (gpointer) (intptr_t) 1);
              if (right_shift_pressed)
                g_idle_add(adjust_delay_idle, (gpointer) (intptr_t) 2);
            }
            else if (ev.code == KEY_MINUS || ev.code == KEY_KPMINUS) {
              if (left_shift_pressed)
                g_idle_add(adjust_delay_idle, (gpointer) (intptr_t) -1);
              if (right_shift_pressed)
                g_idle_add(adjust_delay_idle, (gpointer) (intptr_t) -2);
            }
          }
        }

        // Hotkey: backtick (`) to toggle bot
        if (ev.type == EV_KEY && ev.code == KEY_GRAVE && ev.value == 1) {
          bool current = bot_active.load();
          bot_active   = !current;

          if (bot_active)
            bot.start();
          else
            bot.stop();

          g_idle_add(update_toggle_btn_idle, NULL);
        }

        // Hotkey: 1 for White
        if (ev.type == EV_KEY && ev.code == KEY_1 && ev.value == 1) {
          bot.set_playing_white(true);
          g_idle_add(update_color_combo_idle, (gpointer) (intptr_t) true);
        }

        // Hotkey: 2 for Black
        if (ev.type == EV_KEY && ev.code == KEY_2 && ev.value == 1) {
          bot.set_playing_white(false);
          g_idle_add(update_color_combo_idle, (gpointer) (intptr_t) false);
        }

        // Hotkey: R for Reset Game
        if (ev.type == EV_KEY && ev.code == KEY_R && ev.value == 1) {
          g_idle_add(reset_game_idle, NULL);
        }

        // Hotkey: C for Calibrate
        if (ev.type == EV_KEY && ev.code == KEY_C && ev.value == 1) {
          g_idle_add(calibrate_idle, NULL);
        }

        // Calibration click detection (real mouse click, not virtual)
        if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1) {
          if (calib_state == 1 || calib_state == 2) {
            // Read current cursor position from X11
            // We'll use xdotool-style query via XQueryPointer
            Display* dpy = XOpenDisplay(nullptr);
            if (dpy) {
              Window       root = DefaultRootWindow(dpy);
              Window       child;
              int          root_x, root_y, win_x, win_y;
              unsigned int mask;
              XQueryPointer(dpy, root, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
              XCloseDisplay(dpy);

              if (calib_state == 1) {
                calib_x1    = root_x;
                calib_y1    = root_y;
                calib_state = 2;

                g_idle_add(
                  +[](gpointer data) -> gboolean {
                    gtk_button_set_label(GTK_BUTTON(calibrate_btn), "Click BOTTOM-RIGHT (h1)");
                    return G_SOURCE_REMOVE;
                  },
                  NULL
                );

                auto* upd = new StatusUpdate{"CALIBRATING: Now click BOTTOM-RIGHT corner (h1)"};
                g_idle_add(update_status_idle, upd);
              }
              else if (calib_state == 2) {
                bot.calibrate(calib_x1, calib_y1, root_x, root_y);
                calib_state = 3;

                auto* upd = new StatusUpdate{"Board calibrated! Ready to start."};
                g_idle_add(update_status_idle, upd);

                // Reset calibrate button label
                g_idle_add(
                  +[](gpointer data) -> gboolean {
                    gtk_button_set_label(GTK_BUTTON(calibrate_btn), "(C)alibrate Board");
                    return G_SOURCE_REMOVE;
                  },
                  NULL
                );
              }
            }
          }
        }
      }
    }
  }

  for (int fd : fds)
    close(fd);
  close(epfd);
}

/* ============================================================
 * GTK Application
 * ============================================================ */

static void apply_css(GtkWidget* widget)
{
  GtkCssProvider* provider = gtk_css_provider_new();

  const char* css = "window {"
                    "  background-color: #11111b;"
                    "}"
                    "label {"
                    "  color: #cdd6f4;"
                    "  font-family: 'Inter', 'Segoe UI', sans-serif;"
                    "  font-size: 16px;"
                    "}"
                    "label.title {"
                    "  font-size: 24px;"
                    "  font-weight: 800;"
                    "  color: #89b4fa;"
                    "}"
                    "label.section {"
                    "  font-size: 18px;"
                    "  font-weight: 700;"
                    "  color: #cba6f7;"
                    "  margin-top: 8px;"
                    "  margin-bottom: 4px;"
                    "}"
                    "label.status {"
                    "  font-size: 16px;"
                    "  color: #a6e3a1;"
                    "  font-weight: 600;"
                    "}"
                    "label.mono {"
                    "  font-size: 14px;"
                    "  color: #6c7086;"
                    "  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
                    "}"
                    "button, togglebutton {"
                    "  background: linear-gradient(180deg, #313244, #1e1e2e);"
                    "  color: #cdd6f4;"
                    "  border: 1px solid #45475a;"
                    "  border-radius: 8px;"
                    "  padding: 10px 16px;"
                    "  font-family: 'Inter', sans-serif;"
                    "  font-size: 16px;"
                    "  font-weight: 600;"
                    "  box-shadow: 0 2px 4px rgba(0,0,0,0.2);"
                    "  transition: all 0.2s ease-in-out;"
                    "}"
                    "button:hover {"
                    "  background: linear-gradient(180deg, #45475a, #313244);"
                    "  border-color: #89b4fa;"
                    "  box-shadow: 0 4px 8px rgba(0,0,0,0.3);"
                    "}"
                    "button:active {"
                    "  background: #1e1e2e;"
                    "  box-shadow: inset 0 2px 4px rgba(0,0,0,0.4);"
                    "}"
                    "button.toggle-on {"
                    "  background: linear-gradient(180deg, #f38ba8, #eba0ac);"
                    "  color: #11111b;"
                    "  border-color: #f38ba8;"
                    "  box-shadow: 0 0 10px rgba(243,139,168,0.4);"
                    "}"
                    "button.toggle-on:hover {"
                    "  background: linear-gradient(180deg, #eba0ac, #f38ba8);"
                    "}"
                    "scale trough {"
                    "  background-color: #313244;"
                    "  border-radius: 4px;"
                    "  min-height: 6px;"
                    "}"
                    "scale highlight {"
                    "  background-color: #89b4fa;"
                    "  border-radius: 4px;"
                    "}"
                    "scale slider {"
                    "  background-color: #cdd6f4;"
                    "  border-radius: 50%;"
                    "  min-width: 16px;"
                    "  min-height: 16px;"
                    "  box-shadow: 0 2px 4px rgba(0,0,0,0.3);"
                    "}"
                    "combobox button {"
                    "  background: #1e1e2e;"
                    "  color: #cdd6f4;"
                    "  border-color: #45475a;"
                    "}"
                    "spinbutton {"
                    "  background: #1e1e2e;"
                    "  color: #cdd6f4;"
                    "  border: 1px solid #45475a;"
                    "  border-radius: 6px;"
                    "  padding: 4px;"
                    "}"
                    "separator {"
                    "  background-color: #313244;"
                    "  min-height: 1px;"
                    "  margin: 8px 0;"
                    "}"
                    "textview {"
                    "  background-color: #1e1e2e;"
                    "  color: #a6adc8;"
                    "}"
                    "textview text {"
                    "  background-color: #1e1e2e;"
                    "  color: #a6adc8;"
                    "  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
                    "  font-size: 12px;"
                    "}"
                    "scrolledwindow {"
                    "  border: 1px solid #313244;"
                    "  border-radius: 8px;"
                    "  background-color: #1e1e2e;"
                    "}";

  gtk_css_provider_load_from_data(provider, css, -1, NULL);
  gtk_style_context_add_provider_for_screen(
    gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
  );
  g_object_unref(provider);
}

static GtkWidget* create_label(const char* text, const char* css_class)
{
  GtkWidget* label = gtk_label_new(text);
  gtk_widget_set_halign(label, GTK_ALIGN_START);

  if (css_class) {
    GtkStyleContext* ctx = gtk_widget_get_style_context(label);
    gtk_style_context_add_class(ctx, css_class);
  }

  return label;
}

static void activate(GtkApplication* app, gpointer user_data)
{
  // Prevent double windows if second instance tries to launch
  GList* windows = gtk_application_get_windows(app);
  if (windows != NULL) {
    gtk_window_present(GTK_WINDOW(windows->data));
    return;
  }

  GtkWidget* window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "ChessyNotCheesy");
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 460);
  gtk_container_set_border_width(GTK_CONTAINER(window), 16);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

  g_signal_connect(
    window, "window-state-event",
    G_CALLBACK(+[](GtkWidget* widget, GdkEventWindowState* event, gpointer user_data) -> gboolean {
      if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) {
        g_window_visible = !(event->new_window_state & GDK_WINDOW_STATE_ICONIFIED);
      }
      return FALSE;
    }),
    NULL
  );

  // Set the application icon
  GError* error = NULL;
  if (!gtk_window_set_icon_from_file(GTK_WINDOW(window), "images/Icon_256.png", &error)) {
    fprintf(stderr, "[GUI] Warning: Could not load icon: %s\n", error->message);
    g_error_free(error);
  }

  apply_css(window);

  GtkWidget* main_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_container_add(GTK_CONTAINER(window), main_hbox);

  GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_size_request(vbox, 260, -1);
  gtk_box_pack_start(GTK_BOX(main_hbox), vbox, FALSE, FALSE, 0);

  // ── Top Bar ──
  GtkWidget* top_check = gtk_check_button_new_with_label("Always on Top");
  g_signal_connect(top_check, "toggled", G_CALLBACK(on_always_on_top_toggled), window);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(top_check), TRUE);
  gtk_widget_set_halign(top_check, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(vbox), top_check, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

  // Calibration ──
  calibrate_btn = gtk_button_new_with_label("(C)alibrate");
  g_signal_connect(calibrate_btn, "clicked", G_CALLBACK(on_calibrate_clicked), NULL);
  gtk_box_pack_start(GTK_BOX(vbox), calibrate_btn, FALSE, FALSE, 0);

  GtkWidget* reset_btn = gtk_button_new_with_label("(R)eset Game");
  g_signal_connect(
    reset_btn, "clicked",
    G_CALLBACK(+[](GtkWidget* widget, gpointer data) { g_idle_add(reset_game_idle, NULL); }), NULL
  );
  gtk_box_pack_start(GTK_BOX(vbox), reset_btn, FALSE, FALSE, 0);

  // ── Color Selector ──
  GtkWidget* color_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(vbox), color_hbox, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(color_hbox), create_label("Playing as:", NULL), FALSE, FALSE, 0);

  color_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "White (1)");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(color_combo), "Black (2)");
  gtk_combo_box_set_active(GTK_COMBO_BOX(color_combo), 0);
  g_signal_connect(color_combo, "changed", G_CALLBACK(on_color_changed), NULL);
  gtk_box_pack_start(GTK_BOX(color_hbox), color_combo, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

  // ── Engine Settings ──
  gtk_box_pack_start(GTK_BOX(vbox), create_label("Engine Settings", "section"), FALSE, FALSE, 0);

  // Depth slider
  GtkWidget* depth_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(vbox), depth_hbox, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(depth_hbox), create_label("Depth:", NULL), FALSE, FALSE, 0);

  //                                           depth, min, max, step, page_size, page_increment
  GtkAdjustment* depth_adj = gtk_adjustment_new(3.0, 1.0, 30.0, 1.0, 5.0, 0.0);
  depth_scale              = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, depth_adj);
  gtk_scale_set_digits(GTK_SCALE(depth_scale), 0);
  gtk_scale_set_value_pos(GTK_SCALE(depth_scale), GTK_POS_RIGHT);
  gtk_widget_set_hexpand(depth_scale, TRUE);
  g_signal_connect(depth_scale, "value-changed", G_CALLBACK(on_depth_changed), NULL);
  gtk_box_pack_start(GTK_BOX(depth_hbox), depth_scale, TRUE, TRUE, 0);

  // CPU Threads
  GtkWidget* cpu_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(vbox), cpu_hbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(cpu_hbox), create_label("CPU Threads:", NULL), FALSE, FALSE, 0);

  cpu_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cpu_combo), "Single Core");

  long num_procs = sysconf(_SC_NPROCESSORS_ONLN);
  if (num_procs < 1)
    num_procs = 1;

  if (num_procs > 1) {
    std::string multi_text = "Multi-Core (" + std::to_string(num_procs) + " Cores)";
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cpu_combo), multi_text.c_str());
  }

  // Create CPU Spin button first so combo box can set it
  cpu_spin = gtk_spin_button_new_with_range(1, num_procs, 1);
  g_signal_connect(cpu_spin, "value-changed", G_CALLBACK(on_cpu_spin_changed), NULL);
  g_signal_connect(cpu_spin, "input", G_CALLBACK(on_cpu_spin_input), NULL);
  g_signal_connect(cpu_spin, "activate", G_CALLBACK(on_spin_activate), NULL);

  g_signal_connect(cpu_combo, "changed", G_CALLBACK(on_cpu_changed), NULL);
  gtk_combo_box_set_active(GTK_COMBO_BOX(cpu_combo), 0);

  gtk_box_pack_start(GTK_BOX(cpu_hbox), cpu_combo, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(cpu_hbox), cpu_spin, TRUE, TRUE, 0);

  // Memory Hash
  GtkWidget* mem_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(vbox), mem_hbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(mem_hbox), create_label("RAM Hash (MB):", NULL), FALSE, FALSE, 0);

  memory_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(memory_combo), "256 MB");

  long phys_pages = sysconf(_SC_PHYS_PAGES);
  long page_size  = sysconf(_SC_PAGE_SIZE);
  long ram_mb     = (phys_pages * page_size) / (1024 * 1024);
  long max_safe_ram =
    ram_mb / 2;  // Prevent allocating 100% of physical RAM, which triggers OOM Killer!
  if (max_safe_ram < 256)
    max_safe_ram = 256;

  if (ram_mb > 1024)
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(memory_combo), "512 MB");
  if (ram_mb > 2048)
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(memory_combo), "1024 MB");
  if (ram_mb > 4096)
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(memory_combo), "2048 MB");

  memory_spin = gtk_spin_button_new_with_range(16, max_safe_ram, 128);
  g_signal_connect(memory_spin, "value-changed", G_CALLBACK(on_memory_spin_changed), NULL);
  g_signal_connect(memory_spin, "input", G_CALLBACK(on_memory_spin_input), NULL);
  g_signal_connect(memory_spin, "activate", G_CALLBACK(on_spin_activate), NULL);

  int default_mem_idx = 0;
  if (ram_mb > 2048)
    default_mem_idx = 2;  // 1024 MB
  else if (ram_mb > 1024)
    default_mem_idx = 1;  // 512 MB

  g_signal_connect(memory_combo, "changed", G_CALLBACK(on_memory_changed), NULL);
  gtk_combo_box_set_active(GTK_COMBO_BOX(memory_combo), default_mem_idx);

  gtk_box_pack_start(GTK_BOX(mem_hbox), memory_combo, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(mem_hbox), memory_spin, TRUE, TRUE, 0);

  // Move delay
  GtkWidget* delay_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_pack_start(GTK_BOX(vbox), delay_vbox, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(delay_vbox), create_label("Mouse Delay (ms):", NULL), FALSE, FALSE, 0);

  GtkWidget* delay_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(delay_vbox), delay_hbox, FALSE, FALSE, 0);

  //                                           value, min, max, step, page_size, page_increment
  GtkAdjustment* delay_min_adj = gtk_adjustment_new(1000.0, 50.0, 5000.0, 50.0, 100.0, 0.0);
  delay_min_spin               = gtk_spin_button_new(delay_min_adj, 50.0, 0);
  g_signal_connect(delay_min_spin, "value-changed", G_CALLBACK(on_delay_changed), NULL);
  g_signal_connect(delay_min_spin, "activate", G_CALLBACK(on_spin_activate), NULL);
  gtk_box_pack_start(GTK_BOX(delay_hbox), delay_min_spin, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(delay_hbox), create_label("-", NULL), FALSE, FALSE, 0);

  //                                           value, min, max, step, page_size, page_increment
  GtkAdjustment* delay_max_adj = gtk_adjustment_new(5000.0, 50.0, 5000.0, 50.0, 100.0, 0.0);
  delay_max_spin               = gtk_spin_button_new(delay_max_adj, 50.0, 0);
  g_signal_connect(delay_max_spin, "value-changed", G_CALLBACK(on_delay_changed), NULL);
  g_signal_connect(delay_max_spin, "activate", G_CALLBACK(on_spin_activate), NULL);
  gtk_box_pack_start(GTK_BOX(delay_hbox), delay_max_spin, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

  // ── Start / Stop ──
  toggle_btn = gtk_toggle_button_new_with_label("▶ Start Bot (Key: `)");
  g_signal_connect(toggle_btn, "toggled", G_CALLBACK(on_toggle), NULL);
  gtk_box_pack_start(GTK_BOX(vbox), toggle_btn, FALSE, FALSE, 0);

  // Initialize bot settings from default GUI values
  on_color_changed(GTK_COMBO_BOX(color_combo), NULL);
  on_depth_changed(GTK_RANGE(depth_scale), NULL);
  on_delay_changed(GTK_SPIN_BUTTON(delay_min_spin), NULL);

  GtkWidget* hotkey_hint = create_label("Hotkey: ` (backtick) to toggle", "mono");
  gtk_widget_set_halign(hotkey_hint, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(vbox), hotkey_hint, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

  // ── Status Display ──
  gtk_box_pack_start(GTK_BOX(vbox), create_label("Status", "section"), FALSE, FALSE, 0);

  status_label = create_label("Initializing...", "status");
  gtk_label_set_line_wrap(GTK_LABEL(status_label), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(status_label), 32);
  gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);

  // ── PGN Sidebar ──
  gtk_box_pack_start(
    GTK_BOX(main_hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0
  );

  GtkWidget* pgn_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_pack_start(GTK_BOX(main_hbox), pgn_vbox, TRUE, TRUE, 0);

  GtkWidget* pgn_title = create_label("Move History (PGN)", "section");
  gtk_box_pack_start(GTK_BOX(pgn_vbox), pgn_title, FALSE, FALSE, 0);

  GtkWidget* scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC
  );
  gtk_widget_set_size_request(scrolled_window, 200, -1);
  gtk_box_pack_start(GTK_BOX(pgn_vbox), scrolled_window, TRUE, TRUE, 0);

  pgn_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(pgn_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(pgn_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(pgn_view), GTK_WRAP_WORD);
  gtk_container_add(GTK_CONTAINER(scrolled_window), pgn_view);

  pgn_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pgn_view));

  gtk_widget_show_all(window);

  // Apply always-on-top after the window is realized
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(top_check))) {
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
  }

  // Set up bot callbacks for GUI updates
  bot.on_status_change = [](const std::string& status) {
    auto* upd = new StatusUpdate{status};
    g_idle_add(update_status_idle, upd);
  };

  bot.on_move_made = [](const std::string& move) {
    auto* upd = new StatusUpdate{move};
    g_idle_add(update_move_idle, upd);
  };

  bot.on_color_detected = [](bool is_white) {
    g_idle_add(update_color_combo_idle, (gpointer) (intptr_t) is_white);
  };

  // Initialize bot subsystems
  if (!bot.init()) {
    gtk_label_set_text(GTK_LABEL(status_label), "INIT FAILED — check console for errors");
  }
}

/* ============================================================
 * Entry point for GUI
 * ============================================================ */

#include <X11/Xlib.h>

int run_gui(int argc, char** argv, BotController& bot_ref, std::atomic<bool>& bot_active_ref)
{
  g_bot        = &bot_ref;
  g_bot_active = &bot_active_ref;

  XInitThreads();

  // Start input listener thread (hotkeys + calibration clicks)
  std::thread input_thread(input_listener_thread);
  input_thread.detach();

  // Launch GTK application (default flags ensures single-instance behavior)
  GtkApplication* app =
    gtk_application_new("org.riik.ChessyNotCheesy", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}
