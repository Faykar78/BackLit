/*
 * kb_gui.c - Keyboard Backlight GTK4 GUI with Hotkey Support
 * 
 * Direct EC control via sysfs interface
 * Includes evdev monitoring for Fn key hotkeys
 * Build: gcc -o kb_gui kb_gui.c `pkg-config --cflags --libs gtk4` -lpthread
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/input.h>
#include <dirent.h>
#include <signal.h>
#include <math.h>

#define SYSFS_PATH "/sys/devices/platform/clevo_xsm_wmi"

/* Color definitions */
typedef struct {
    const char *name;
    const char *hex;
} Color;

static const Color colors[] = {
    {"blue",    "#0066FF"},
    {"cyan",    "#00DDFF"},
    {"green",   "#00FF66"},
    {"lime",    "#88FF00"},
    {"yellow",  "#FFFF00"},
    {"orange",  "#FF8800"},
    {"red",     "#FF2222"},
    {"pink",    "#FF44AA"},
    {"magenta", "#FF00FF"},
    {"purple",  "#AA44FF"},
    {"teal",    "#00AAAA"},
    {"white",   "#FFFFFF"},
};
#define NUM_COLORS 12

/* Global widgets */
static GtkWidget *power_btn;
static GtkWidget *brightness_scale;
static GtkWidget *brightness_label;
static GtkWidget *wave_switch;
static GtkWidget *status_label;
static GtkWidget *color_buttons[NUM_COLORS];
static GtkWidget *color_wheel = NULL;
static int selected_color = 0;
static double current_hue = 0.5; /* 0-1, maps to 0-360 degrees */

/* Hotkey thread */
static pthread_t input_thread;
static volatile int input_running = 0;
static volatile int is_backlight_on = 1;

/* Forward declarations */
static void update_status(const char *msg);

/* Sysfs read/write */
static int read_sysfs(const char *attr, char *buf, size_t bufsize)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", SYSFS_PATH, attr);
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t n = read(fd, buf, bufsize - 1);
    close(fd);
    
    if (n < 0) return -1;
    buf[n] = '\0';
    if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';
    
    return 0;
}

/* Write to sysfs directly (requires udev rules for permissions) */
static int write_sysfs(const char *attr, const char *value)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", SYSFS_PATH, attr);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        /* Fallback to sudo tee if direct write fails */
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "echo '%s' | sudo tee %s > /dev/null 2>&1", 
                 value, path);
        return system(cmd);
    }
    
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    return n < 0 ? -1 : 0;
}

/* Get/Set functions */
static int kb_get_state(void)
{
    char buf[16];
    if (read_sysfs("kb_state", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

static int kb_get_brightness(void)
{
    char buf[16];
    if (read_sysfs("kb_brightness", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

static int kb_get_wave(void)
{
    char buf[16];
    if (read_sysfs("kb_wave", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

/* static void kb_set_state(int on)
{
    write_sysfs("kb_state", on ? "1" : "0");
} */

static void kb_set_brightness(int level)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", level);
    write_sysfs("kb_brightness", buf);
}

static void kb_set_color(const char *color)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s %s", color, color, color);
    write_sysfs("kb_color", buf);
}

static void kb_set_wave(int on)
{
    write_sysfs("kb_wave", on ? "1" : "0");
}

/* HSV to RGB conversion (h: 0-360, s,v: 0-1) -> r,g,b: 0-1 */
/* static void hsv_to_rgb(double h, double s, double v, double *r, double *g, double *b)
{
    int i = (int)(h / 60.0) % 6;
    double f = h / 60.0 - i;
    double p = v * (1 - s);
    double q = v * (1 - f * s);
    double t = v * (1 - (1 - f) * s);
    
    switch (i) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
} */

/* Get color name from hue (map to preset colors) */
/* static const char* hue_to_color_name(double hue)
{
    if (hue < 30) return "red";
    if (hue < 60) return "orange";
    if (hue < 90) return "yellow";
    if (hue < 120) return "lime";
    if (hue < 150) return "green";
    if (hue < 180) return "teal";
    if (hue < 210) return "cyan";
    if (hue < 240) return "blue";
    if (hue < 270) return "purple";
    if (hue < 300) return "magenta";
    if (hue < 330) return "pink";
    return "red";
} */
/* Parse hex color to RGB */
static void hex_to_rgb(const char *hex, double *r, double *g, double *b)
{
    int ri, gi, bi;
    sscanf(hex, "#%02x%02x%02x", &ri, &gi, &bi);
    *r = ri / 255.0;
    *g = gi / 255.0;
    *b = bi / 255.0;
}

/* Draw color wheel using preset colors */
static void draw_color_wheel(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    double cx = width / 2.0;
    double cy = height / 2.0;
    double outer_r = (width < height ? width : height) / 2.0 - 5;
    double inner_r = outer_r * 0.65;
    
    /* Draw segments using preset colors */
    for (int i = 0; i < NUM_COLORS; i++) {
        double angle1 = (i * 360.0 / NUM_COLORS) * G_PI / 180.0;
        double angle2 = ((i + 1) * 360.0 / NUM_COLORS) * G_PI / 180.0;
        
        double r, g, b;
        hex_to_rgb(colors[i].hex, &r, &g, &b);
        
        cairo_set_source_rgb(cr, r, g, b);
        cairo_arc(cr, cx, cy, outer_r, angle1 - G_PI/2, angle2 - G_PI/2);
        cairo_arc_negative(cr, cx, cy, inner_r, angle2 - G_PI/2, angle1 - G_PI/2);
        cairo_close_path(cr);
        cairo_fill(cr);
    }
    
    /* Draw center circle with selected color */
    double r, g, b;
    hex_to_rgb(colors[selected_color].hex, &r, &g, &b);
    cairo_set_source_rgb(cr, r * 0.3, g * 0.3, b * 0.3);
    cairo_arc(cr, cx, cy, inner_r - 3, 0, 2 * G_PI);
    cairo_fill(cr);
    
    /* Draw "Color" text in center */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    
    cairo_text_extents_t extents;
    cairo_text_extents(cr, "Color", &extents);
    cairo_move_to(cr, cx - extents.width/2, cy + extents.height/2);
    cairo_show_text(cr, "Color");
    
    /* Draw selection indicator */
    double sel_angle = (selected_color * 360.0 / NUM_COLORS + 15) * G_PI / 180.0 - G_PI/2;
    double sel_r = (outer_r + inner_r) / 2;
    double sel_x = cx + cos(sel_angle) * sel_r;
    double sel_y = cy + sin(sel_angle) * sel_r;
    
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_line_width(cr, 3);
    cairo_arc(cr, sel_x, sel_y, 8, 0, 2 * G_PI);
    cairo_stroke(cr);
}

/* Color wheel click handler */
static void on_color_wheel_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer data)
{
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);
    
    double cx = width / 2.0;
    double cy = height / 2.0;
    double outer_r = (width < height ? width : height) / 2.0 - 5;
    double inner_r = outer_r * 0.65;
    
    /* Check if click is within the ring */
    double dx = x - cx;
    double dy = y - cy;
    double dist = sqrt(dx*dx + dy*dy);
    
    if (dist >= inner_r && dist <= outer_r) {
        /* Calculate which color segment was clicked */
        double angle = atan2(dy, dx) + G_PI/2;
        if (angle < 0) angle += 2 * G_PI;
        
        int idx = (int)(angle / (2 * G_PI) * NUM_COLORS) % NUM_COLORS;
        selected_color = idx;
        
        /* Update hardware */
        kb_set_color(colors[idx].name);
        
        /* Update status */
        char buf[64];
        snprintf(buf, sizeof(buf), "Color: %s", colors[idx].name);
        update_status(buf);
        
        /* Redraw wheel */
        gtk_widget_queue_draw(widget);
    }
}

/* Update status label */
static void update_status(const char *msg)
{
    gtk_label_set_text(GTK_LABEL(status_label), msg);
}

/* Callbacks */
static void on_power_toggled(GtkToggleButton *btn, gpointer data)
{
    gboolean active = gtk_toggle_button_get_active(btn);
    is_backlight_on = active ? 1 : 0;
    
    if (active) {
        /* Turn ON: restore color and brightness */
        kb_set_color(colors[selected_color].name);
        kb_set_brightness(0);
        gtk_range_set_value(GTK_RANGE(brightness_scale), 0);
        gtk_label_set_text(GTK_LABEL(brightness_label), "0");
    } else {
        /* Turn OFF: set color to black (truly powers off LEDs) */
        kb_set_color("black");
    }
    
    gtk_button_set_label(GTK_BUTTON(btn), active ? "ON" : "OFF");
    update_status(active ? "Backlight ON" : "Backlight OFF");
}

static void on_brightness_changed(GtkRange *range, gpointer data)
{
    int value = (int)gtk_range_get_value(range);
    kb_set_brightness(value);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    gtk_label_set_text(GTK_LABEL(brightness_label), buf);
    
    snprintf(buf, sizeof(buf), "Brightness: %d", value);
    update_status(buf);
}

static void on_color_clicked(GtkButton *btn, gpointer data)
{
    int idx = GPOINTER_TO_INT(data);
    
    // Update selection visually 
    for (int i = 0; i < NUM_COLORS; i++) {
        if (i == idx) {
            gtk_widget_add_css_class(color_buttons[i], "selected");
        } else {
            gtk_widget_remove_css_class(color_buttons[i], "selected");
        }
    }
    
    kb_set_color(colors[idx].name);
    selected_color = idx;
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Color: %s", colors[idx].name);
    update_status(buf);
} */

static void on_wave_toggled(GObject *sw, GParamSpec *pspec, gpointer data)
{
    gboolean active = gtk_switch_get_active(GTK_SWITCH(sw));
    
    if (active) {
        /* Wave requires kernel module reload to start animation */
        update_status("Reloading module for wave...");
        system("sudo rmmod clevo-xsm-wmi 2>/dev/null; "
               "sudo modprobe clevo-xsm-wmi");
        kb_set_wave(1);
        update_status("Wave Enabled");
    } else {
        kb_set_wave(0);
        update_status("Wave Disabled");
    }
}

/* Apply CSS styles */
static void apply_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    
    const char *css = 
        /* Main window - deep gradient background */
        "window {"
        "  background: linear-gradient(160deg, #0f0c29 0%, #302b63 50%, #24243e 100%);"
        "}"
        
        /* Title styling */
        ".title-label {"
        "  font-size: 26px;"
        "  font-weight: 800;"
        "  letter-spacing: 1px;"
        "  color: white;"
        "  margin: 15px 0;"
        "  text-shadow: 0 2px 10px rgba(0, 200, 255, 0.3);"
        "}"
        
        /* Glassmorphism section frames */
        ".section-frame {"
        "  background: rgba(255, 255, 255, 0.05);"
        "  border: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-radius: 16px;"
        "  padding: 18px;"
        "  margin: 8px 0;"
        "}"
        
        /* Section labels */
        ".section-label {"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  letter-spacing: 0.5px;"
        "  color: rgba(255, 255, 255, 0.85);"
        "}"
        
        /* Power button - premium circular design */
        ".power-btn {"
        "  min-width: 90px;"
        "  min-height: 90px;"
        "  border-radius: 45px;"
        "  font-size: 20px;"
        "  font-weight: 800;"
        "  border: none;"
        "  transition: all 0.3s ease;"
        "}"
        ".power-btn:checked {"
        "  background: linear-gradient(145deg, #00FF88 0%, #00CC66 50%, #00AA44 100%);"
        "  color: #001a0a;"
        "  box-shadow: 0 0 30px rgba(0, 255, 136, 0.5), inset 0 2px 0 rgba(255, 255, 255, 0.3);"
        "}"
        ".power-btn:not(:checked) {"
        "  background: linear-gradient(145deg, #3a3a4a 0%, #2a2a3a 50%, #1a1a2a 100%);"
        "  color: #666;"
        "  box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.3);"
        "}"
        ".power-btn:hover {"
        "  transform: scale(1.02);"
        "}"
        
        /* Color buttons - vibrant with glow */
        ".color-btn {"
        "  min-width: 70px;"
        "  min-height: 38px;"
        "  border-radius: 10px;"
        "  border: 2px solid rgba(255, 255, 255, 0.2);"
        "  font-size: 10px;"
        "  font-weight: 700;"
        "  letter-spacing: 0.5px;"
        "  text-transform: uppercase;"
        "  transition: all 0.2s ease;"
        "}"
        ".color-btn:hover {"
        "  transform: translateY(-2px);"
        "  box-shadow: 0 4px 15px rgba(0, 0, 0, 0.3);"
        "}"
        ".color-btn.selected {"
        "  border: 3px solid #00D4FF;"
        "  box-shadow: 0 0 20px rgba(0, 212, 255, 0.5);"
        "}"
        
        /* Brightness slider styling */
        "scale {"
        "  padding: 10px 0;"
        "}"
        "scale trough {"
        "  background: linear-gradient(90deg, #00D4FF 0%, #FFD700 100%);"
        "  border-radius: 4px;"
        "  min-height: 8px;"
        "}"
        "scale slider {"
        "  background: white;"
        "  border-radius: 50%;"
        "  min-width: 22px;"
        "  min-height: 22px;"
        "  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.4);"
        "}"
        "scale value {"
        "  color: #00D4FF;"
        "  font-weight: bold;"
        "  font-size: 14px;"
        "}"
        "scale marks {"
        "  color: rgba(255, 255, 255, 0.4);"
        "}"
        
        /* Wave switch styling */
        "switch {"
        "  background: #333;"
        "  border-radius: 14px;"
        "  min-width: 50px;"
        "  min-height: 26px;"
        "}"
        "switch:checked {"
        "  background: linear-gradient(90deg, #00D4FF 0%, #00FF88 100%);"
        "}"
        "switch slider {"
        "  background: white;"
        "  border-radius: 50%;"
        "  min-width: 22px;"
        "  min-height: 22px;"
        "}"
        
        /* Brightness value display */
        ".brightness-value {"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #00D4FF;"
        "  text-shadow: 0 0 10px rgba(0, 212, 255, 0.5);"
        "}"
        
        /* Status label */
        ".status-label {"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "  color: rgba(255, 255, 255, 0.4);"
        "  letter-spacing: 0.5px;"
        "}"
        ;
    
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
}

/* Build UI */
static void activate(GtkApplication *app, gpointer user_data)
{
    apply_css();
    
    /* Main window */
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Keyboard Backlight");
    gtk_window_set_default_size(GTK_WINDOW(window), 380, 520);
    
    /* Main box */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    gtk_widget_set_margin_top(main_box, 20);
    gtk_widget_set_margin_bottom(main_box, 20);
    gtk_window_set_child(GTK_WINDOW(window), main_box);
    
    /* Title */
    GtkWidget *title = gtk_label_new("⌨️ Keyboard Backlight");
    gtk_widget_add_css_class(title, "title-label");
    gtk_box_append(GTK_BOX(main_box), title);
    
    /* Power section */
    GtkWidget *power_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(power_frame, "section-frame");
    GtkWidget *power_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(power_box, GTK_ALIGN_CENTER);
    gtk_frame_set_child(GTK_FRAME(power_frame), power_box);
    
    GtkWidget *power_label = gtk_label_new("Power");
    gtk_widget_add_css_class(power_label, "section-label");
    gtk_box_append(GTK_BOX(power_box), power_label);
    
    power_btn = gtk_toggle_button_new_with_label(kb_get_state() ? "ON" : "OFF");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(power_btn), kb_get_state());
    gtk_widget_add_css_class(power_btn, "power-btn");
    g_signal_connect(power_btn, "toggled", G_CALLBACK(on_power_toggled), NULL);
    gtk_box_append(GTK_BOX(power_box), power_btn);
    
    gtk_box_append(GTK_BOX(main_box), power_frame);
    
    /* Brightness section */
    GtkWidget *bright_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(bright_frame, "section-frame");
    GtkWidget *bright_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_frame_set_child(GTK_FRAME(bright_frame), bright_box);
    
    GtkWidget *bright_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *bright_title = gtk_label_new("☀️ Brightness");
    gtk_widget_add_css_class(bright_title, "section-label");
    gtk_box_append(GTK_BOX(bright_header), bright_title);
    
    brightness_label = gtk_label_new("0");
    gtk_widget_add_css_class(brightness_label, "brightness-value");
    gtk_widget_set_hexpand(brightness_label, TRUE);
    gtk_widget_set_halign(brightness_label, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(bright_header), brightness_label);
    gtk_box_append(GTK_BOX(bright_box), bright_header);
    
    brightness_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 9, 1);
    gtk_scale_set_draw_value(GTK_SCALE(brightness_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(brightness_scale), GTK_POS_TOP);
    /* Add tick marks for each value */
    for (int i = 0; i <= 9; i++) {
        gtk_scale_add_mark(GTK_SCALE(brightness_scale), i, GTK_POS_BOTTOM, NULL);
    }
    gtk_range_set_value(GTK_RANGE(brightness_scale), kb_get_brightness());
    g_signal_connect(brightness_scale, "value-changed", G_CALLBACK(on_brightness_changed), NULL);
    gtk_box_append(GTK_BOX(bright_box), brightness_scale);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", kb_get_brightness());
    gtk_label_set_text(GTK_LABEL(brightness_label), buf);
    
    GtkWidget *hint = gtk_label_new("← Bright | Dim →");
    gtk_widget_add_css_class(hint, "status-label");
    gtk_box_append(GTK_BOX(bright_box), hint);
    
    gtk_box_append(GTK_BOX(main_box), bright_frame);
    
    /* Color section - Color Wheel */
    GtkWidget *color_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(color_frame, "section-frame");
    GtkWidget *color_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_frame_set_child(GTK_FRAME(color_frame), color_box);
    
    /* Color wheel drawing area */
    color_wheel = gtk_drawing_area_new();
    gtk_widget_set_size_request(color_wheel, 180, 180);
    gtk_widget_set_halign(color_wheel, GTK_ALIGN_CENTER);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(color_wheel), draw_color_wheel, NULL, NULL);
    
    /* Add click gesture */
    GtkGesture *click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_color_wheel_clicked), NULL);
    gtk_widget_add_controller(color_wheel, GTK_EVENT_CONTROLLER(click));
    
    gtk_box_append(GTK_BOX(color_box), color_wheel);
    gtk_box_append(GTK_BOX(main_box), color_frame);
    
    /* Wave section */
    GtkWidget *wave_frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(wave_frame, "section-frame");
    GtkWidget *wave_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_frame_set_child(GTK_FRAME(wave_frame), wave_box);
    
    GtkWidget *wave_label = gtk_label_new("🌊 Wave Effect");
    gtk_widget_add_css_class(wave_label, "section-label");
    gtk_box_append(GTK_BOX(wave_box), wave_label);
    
    wave_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(wave_switch), kb_get_wave());
    gtk_widget_set_hexpand(wave_switch, TRUE);
    gtk_widget_set_halign(wave_switch, GTK_ALIGN_END);
    g_signal_connect(wave_switch, "notify::active", G_CALLBACK(on_wave_toggled), NULL);
    gtk_box_append(GTK_BOX(wave_box), wave_switch);
    
    gtk_box_append(GTK_BOX(main_box), wave_frame);
    
    /* Status label */
    status_label = gtk_label_new("Ready");
    gtk_widget_add_css_class(status_label, "status-label");
    gtk_box_append(GTK_BOX(main_box), status_label);
    
    gtk_window_present(GTK_WINDOW(window));
}

/* Find TUXEDO Keyboard input device */
static int find_tuxedo_keyboard(char *path, size_t pathlen)
{
    DIR *dir = opendir("/dev/input");
    if (!dir) return -1;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        
        char devpath[256], name[256];
        snprintf(devpath, sizeof(devpath), "/dev/input/%s", entry->d_name);
        
        int fd = open(devpath, O_RDONLY);
        if (fd < 0) continue;
        
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            if (strstr(name, "TUXEDO") != NULL) {
                close(fd);
                closedir(dir);
                snprintf(path, pathlen, "%s", devpath);
                return 0;
            }
        }
        close(fd);
    }
    closedir(dir);
    return -1;
}

/* GUI update for toggle - Safe wrapper for main thread */
static gboolean update_power_btn_wrapper(gpointer data)
{
    gboolean active = GPOINTER_TO_INT(data);
    
    if (power_btn) {
        /* Block signal to prevent loop (we already updated hardware) */
        g_signal_handlers_block_by_func(power_btn, on_power_toggled, NULL);
        
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(power_btn), active);
        gtk_button_set_label(GTK_BUTTON(power_btn), active ? "ON" : "OFF");
        update_status(active ? "Backlight ON (Hotkey)" : "Backlight OFF (Hotkey)");
        
        g_signal_handlers_unblock_by_func(power_btn, on_power_toggled, NULL);
    }
    return FALSE; /* Remove source */
}

/* GUI update for brightness - Safe wrapper for main thread */
static gboolean update_brightness_wrapper(gpointer data)
{
    int level = GPOINTER_TO_INT(data);
    
    if (brightness_scale) {
        g_signal_handlers_block_by_func(brightness_scale, on_brightness_changed, NULL);
        
        gtk_range_set_value(GTK_RANGE(brightness_scale), level);
        
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", level);
        gtk_label_set_text(GTK_LABEL(brightness_label), buf);
        
        snprintf(buf, sizeof(buf), "Brightness: %d (Hotkey)", level);
        update_status(buf);
        
        g_signal_handlers_unblock_by_func(brightness_scale, on_brightness_changed, NULL);
    }
    return FALSE; /* Remove source */
}

/* Hotkey toggle function */
static void hotkey_toggle(void)
{
    if (is_backlight_on) {
        kb_set_color("black");
        is_backlight_on = 0;
    } else {
        kb_set_color(colors[selected_color].name);
        kb_set_brightness(0);
        is_backlight_on = 1;
    }
    
    /* Update GUI safely on main thread */
    g_idle_add(update_power_btn_wrapper, GINT_TO_POINTER(is_backlight_on));
}

/* Hotkey brightness change */
static void hotkey_brightness(int delta)
{
    char buf[16];
    if (read_sysfs("kb_brightness", buf, sizeof(buf)) < 0) return;
    
    int level = atoi(buf) + delta;
    if (level < 0) level = 0;
    if (level > 9) level = 9;
    
    /* Update hardware */
    kb_set_brightness(level);
    
    /* Update GUI safely on main thread */
    g_idle_add(update_brightness_wrapper, GINT_TO_POINTER(level));
}

/* Input monitoring thread */
static void *input_thread_func(void *arg)
{
    char devpath[256];
    
    if (find_tuxedo_keyboard(devpath, sizeof(devpath)) < 0) {
        fprintf(stderr, "TUXEDO Keyboard input device not found\n");
        return NULL;
    }
    
    int fd = open(devpath, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Cannot open input device: %s\n", devpath);
        return NULL;
    }
    
    printf("Hotkey monitoring started on %s\n", devpath);
    
    struct input_event ev;
    while (input_running) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != sizeof(ev)) continue;
        
        /* Only process key press events (value=1) */
        if (ev.type == EV_KEY && ev.value == 1) {
            switch (ev.code) {
            case KEY_KBDILLUMTOGGLE:  /* 228 */
                hotkey_toggle();
                break;
            case KEY_KBDILLUMDOWN:    /* 229 */
                hotkey_brightness(1);  /* Higher number = dimmer */
                break;
            case KEY_KBDILLUMUP:      /* 230 */
                hotkey_brightness(-1); /* Lower number = brighter */
                break;
            }
        }
    }
    
    close(fd);
    return NULL;
}

int main(int argc, char *argv[])
{
    /* Check availability */
    if (access(SYSFS_PATH, F_OK) != 0) {
        fprintf(stderr, "Error: Keyboard backlight not available\n");
        fprintf(stderr, "Make sure clevo_xsm_wmi module is loaded\n");
        return 1;
    }
    
    /* Start input monitoring thread */
    input_running = 1;
    if (pthread_create(&input_thread, NULL, input_thread_func, NULL) != 0) {
        fprintf(stderr, "Warning: Could not start hotkey thread\n");
    }
    
    GtkApplication *app = gtk_application_new("org.clevo.keyboard", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    
    /* Cleanup */
    input_running = 0;
    pthread_join(input_thread, NULL);
    
    g_object_unref(app);
    
    return status;
}
