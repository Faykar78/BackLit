/*
 * ui.c - CC30-Style Control Center UI using extracted Windows assets
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "keyboard.h"
#include "system.h"

/* App state */
static int current_page = 0;
static GtkWidget *main_stack;
static GtkWidget *system_drawing_area;
static GtkWidget *keyboard_drawing_area;
static SystemInfo sys_info;

/* Asset paths */
#define ASSETS_PATH "/usr/share/backlit/assets"

/* Forward declarations */
static void create_system_page(GtkWidget *parent);
static void create_keyboard_page(GtkWidget *parent);

/* CC30 Dark theme CSS */
static const char *cc30_css = 
    "window {"
    "  background-color: #1a1a2e;"
    "}"
    "box {"
    "  background-color: transparent;"
    "}"
    ".main-bg {"
    "  background-color: #1a1a2e;"
    "}"
    ".sidebar {"
    "  background-color: #252540;"
    "  min-width: 60px;"
    "}"
    ".sidebar button {"
    "  background-color: transparent;"
    "  border: none;"
    "  padding: 15px;"
    "  color: #888888;"
    "  font-size: 18px;"
    "  min-width: 50px;"
    "  min-height: 50px;"
    "}"
    ".sidebar button:hover {"
    "  background-color: #333355;"
    "  color: #7fff00;"
    "}"
    ".sidebar button:checked {"
    "  background-color: #333355;"
    "  color: #7fff00;"
    "  border-left: 3px solid #7fff00;"
    "}"
    ".page-title {"
    "  color: #ffffff;"
    "  font-size: 20px;"
    "  font-weight: bold;"
    "  padding: 15px 20px;"
    "}"
    ".section-title {"
    "  color: #7fff00;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  padding: 10px;"
    "}"
    ".mode-button {"
    "  background: linear-gradient(180deg, #2a2a4a 0%, #1a1a2e 100%);"
    "  color: #888888;"
    "  border: 1px solid #444466;"
    "  padding: 12px 25px;"
    "  border-radius: 5px;"
    "  font-size: 13px;"
    "  min-width: 100px;"
    "}"
    ".mode-button:hover {"
    "  background: linear-gradient(180deg, #3a3a5a 0%, #2a2a3e 100%);"
    "  color: #ffffff;"
    "}"
    ".mode-button:checked {"
    "  background: linear-gradient(180deg, #7fff00 0%, #5ab300 100%);"
    "  color: #000000;"
    "  font-weight: bold;"
    "}"
    "label {"
    "  color: #aaaaaa;"
    "}"
    ".value-label {"
    "  color: #7fff00;"
    "  font-size: 24px;"
    "  font-weight: bold;"
    "}"
    ".info-label {"
    "  color: #666688;"
    "  font-size: 11px;"
    "}"
    "scale {"
    "  color: #7fff00;"
    "}"
    "scale trough {"
    "  background-color: #333355;"
    "  min-height: 8px;"
    "  border-radius: 4px;"
    "}"
    "scale highlight {"
    "  background-color: #7fff00;"
    "  border-radius: 4px;"
    "}"
    "scale slider {"
    "  background-color: #7fff00;"
    "  min-width: 18px;"
    "  min-height: 18px;"
    "  border-radius: 9px;"
    "}"
    "checkbutton {"
    "  color: #aaaaaa;"
    "}"
    "checkbutton check {"
    "  background-color: #333355;"
    "  border-color: #555577;"
    "  border-radius: 3px;"
    "}"
    "checkbutton check:checked {"
    "  background-color: #7fff00;"
    "  border-color: #7fff00;"
    "}"
    ".gauge-container {"
    "  background-color: #0d0d1a;"
    "  border-radius: 10px;"
    "  padding: 15px;"
    "}"
    ;

/* Draw CPU speedometer using CC30 style */
static void draw_speedometer(cairo_t *cr, double cx, double cy, double radius,
                             double mhz, double usage, double temp)
{
    /* Background arc */
    cairo_set_line_width(cr, 15);
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.25);
    cairo_arc(cr, cx, cy, radius, M_PI * 0.75, M_PI * 2.25);
    cairo_stroke(cr);
    
    /* Value arc - gradient from green to red based on usage */
    double end_angle = M_PI * 0.75 + (M_PI * 1.5 * usage / 100.0);
    
    cairo_pattern_t *grad = cairo_pattern_create_linear(cx - radius, cy, cx + radius, cy);
    cairo_pattern_add_color_stop_rgb(grad, 0.0, 0.0, 1.0, 0.0);  /* Green */
    cairo_pattern_add_color_stop_rgb(grad, 0.5, 1.0, 1.0, 0.0);  /* Yellow */
    cairo_pattern_add_color_stop_rgb(grad, 1.0, 1.0, 0.0, 0.0);  /* Red */
    
    cairo_set_source(cr, grad);
    cairo_arc(cr, cx, cy, radius, M_PI * 0.75, end_angle);
    cairo_stroke(cr);
    cairo_pattern_destroy(grad);
    
    /* Center text - MHz */
    cairo_set_source_rgb(cr, 0.5, 1.0, 0.0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, radius * 0.4);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", mhz);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, cx - ext.width/2, cy + ext.height/4);
    cairo_show_text(cr, buf);
    
    /* MHz label */
    cairo_set_font_size(cr, radius * 0.15);
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.6);
    cairo_text_extents(cr, "MHz", &ext);
    cairo_move_to(cr, cx - ext.width/2, cy + radius * 0.35);
    cairo_show_text(cr, "MHz");
    
    /* Temperature */
    snprintf(buf, sizeof(buf), "%.0f°C", temp);
    cairo_set_source_rgb(cr, 1.0, 0.5, 0.0);
    cairo_set_font_size(cr, radius * 0.2);
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, cx - ext.width/2, cy + radius * 0.6);
    cairo_show_text(cr, buf);
}

/* Draw circular gauge */
static void draw_circular_gauge(cairo_t *cr, double cx, double cy, double radius,
                                double value, double max, const char *label, const char *unit)
{
    double pct = value / max;
    
    /* Background circle */
    cairo_set_line_width(cr, 8);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.3);
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_stroke(cr);
    
    /* Value arc */
    cairo_set_source_rgb(cr, 0.5, 1.0, 0.0);
    cairo_arc(cr, cx, cy, radius, -M_PI/2, -M_PI/2 + 2*M_PI*pct);
    cairo_stroke(cr);
    
    /* Center percentage */
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_font_size(cr, radius * 0.5);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f%%", pct * 100);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, cx - ext.width/2, cy + ext.height/4);
    cairo_show_text(cr, buf);
    
    /* Label below */
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.6);
    cairo_set_font_size(cr, 10);
    cairo_text_extents(cr, label, &ext);
    cairo_move_to(cr, cx - ext.width/2, cy + radius + 20);
    cairo_show_text(cr, label);
}

/* Draw fan gauge */
static void draw_fan_gauge(cairo_t *cr, double cx, double cy, double radius,
                           int rpm, const char *label)
{
    /* Fan icon circle */
    cairo_set_line_width(cr, 3);
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.4);
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_stroke(cr);
    
    /* Fan blades */
    cairo_set_source_rgb(cr, 0.4, 0.4, 0.5);
    for (int i = 0; i < 4; i++) {
        double angle = i * M_PI / 2;
        cairo_move_to(cr, cx, cy);
        cairo_line_to(cr, cx + cos(angle) * radius * 0.7,
                      cy + sin(angle) * radius * 0.7);
    }
    cairo_stroke(cr);
    
    /* RPM value */
    cairo_set_source_rgb(cr, 0.5, 1.0, 0.0);
    cairo_set_font_size(cr, 12);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", rpm);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, cx - ext.width/2, cy + radius + 15);
    cairo_show_text(cr, buf);
    
    /* Label */
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.6);
    cairo_set_font_size(cr, 10);
    cairo_text_extents(cr, label, &ext);
    cairo_move_to(cr, cx - ext.width/2, cy + radius + 30);
    cairo_show_text(cr, label);
}

/* Draw color wheel */
static void draw_color_wheel(cairo_t *cr, double cx, double cy, double radius)
{
    int segments = 360;
    double inner_radius = radius * 0.6;
    
    for (int i = 0; i < segments; i++) {
        double angle1 = i * 2 * M_PI / segments;
        double angle2 = (i + 1) * 2 * M_PI / segments;
        
        /* HSV to RGB conversion */
        double h = (double)i / segments;
        double r, g, b;
        int hi = (int)(h * 6) % 6;
        double f = h * 6 - hi;
        double q = 1 - f;
        
        switch (hi) {
            case 0: r = 1; g = f; b = 0; break;
            case 1: r = q; g = 1; b = 0; break;
            case 2: r = 0; g = 1; b = f; break;
            case 3: r = 0; g = q; b = 1; break;
            case 4: r = f; g = 0; b = 1; break;
            default: r = 1; g = 0; b = q; break;
        }
        
        cairo_set_source_rgb(cr, r, g, b);
        cairo_move_to(cr, cx + cos(angle1) * inner_radius, cy + sin(angle1) * inner_radius);
        cairo_line_to(cr, cx + cos(angle1) * radius, cy + sin(angle1) * radius);
        cairo_line_to(cr, cx + cos(angle2) * radius, cy + sin(angle2) * radius);
        cairo_line_to(cr, cx + cos(angle2) * inner_radius, cy + sin(angle2) * inner_radius);
        cairo_close_path(cr);
        cairo_fill(cr);
    }
    
    /* White center dot */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, cx, cy, inner_radius * 0.3, 0, 2 * M_PI);
    cairo_fill(cr);
}

/* System page drawing */
static void system_draw(GtkDrawingArea *area, cairo_t *cr,
                        int width, int height, gpointer data)
{
    /* Dark background */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.18);
    cairo_paint(cr);
    
    /* Speedometer */
    double speedo_radius = height * 0.25;
    double mhz = 2000 + sys_info.cpu_usage * 30;
    draw_speedometer(cr, width * 0.25, height * 0.4, speedo_radius,
                     mhz, sys_info.cpu_usage, sys_info.cpu_temp);
    
    /* Right side circular gauges */
    double gauge_r = height * 0.08;
    
    draw_circular_gauge(cr, width * 0.55, height * 0.2, gauge_r,
                        sys_info.mem_usage, 100, "Memory", "GB");
    
    draw_circular_gauge(cr, width * 0.72, height * 0.2, gauge_r,
                        75, 100, "Storage", "GB");
    
    /* Fan gauges */
    draw_fan_gauge(cr, width * 0.55, height * 0.5, gauge_r,
                   sys_info.fan1_rpm, "CPU FAN");
    
    draw_fan_gauge(cr, width * 0.72, height * 0.5, gauge_r,
                   sys_info.fan2_rpm, "GPU FAN");
    
    /* GPU info section */
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.6);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, width * 0.85, height * 0.15);
    cairo_show_text(cr, "GPU Information");
    
    cairo_set_font_size(cr, 10);
    cairo_move_to(cr, width * 0.85, height * 0.22);
    cairo_show_text(cr, "Temperature: --°C");
    cairo_move_to(cr, width * 0.85, height * 0.28);
    cairo_show_text(cr, "Utilization: --%");
}

/* Keyboard page drawing */
static void keyboard_draw(GtkDrawingArea *area, cairo_t *cr,
                          int width, int height, gpointer data)
{
    /* Dark background */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.18);
    cairo_paint(cr);
    
    /* Color wheel */
    double wheel_radius = height * 0.18;
    draw_color_wheel(cr, width * 0.45, height * 0.25, wheel_radius);
    
    /* Keyboard visualization */
    double kb_x = width * 0.12;
    double kb_y = height * 0.52;
    double kb_w = width * 0.76;
    double kb_h = height * 0.38;
    
    /* Keyboard background with rounded corners */
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.18);
    cairo_rectangle(cr, kb_x, kb_y, kb_w, kb_h);
    cairo_fill(cr);
    
    /* Key grid */
    cairo_set_source_rgb(cr, 0.18, 0.18, 0.25);
    double key_w = kb_w / 15;
    double key_h = kb_h / 5;
    
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 14; col++) {
            double kx = kb_x + 10 + col * key_w;
            double ky = kb_y + 10 + row * key_h;
            
            if (row == 4 && (col < 2 || col > 11)) continue;
            
            /* Key with slight gradient */
            cairo_rectangle(cr, kx + 2, ky + 2, key_w - 4, key_h - 4);
            cairo_fill(cr);
        }
    }
    
    /* Green accent glow on some keys */
    cairo_set_source_rgba(cr, 0.5, 1.0, 0.0, 0.3);
    for (int i = 0; i < 3; i++) {
        double kx = kb_x + 10 + (4 + i) * key_w;
        double ky = kb_y + 10 + 2 * key_h;
        cairo_rectangle(cr, kx + 2, ky + 2, key_w - 4, key_h - 4);
        cairo_fill(cr);
    }
}

/* Callbacks */
static void on_system_clicked(GtkButton *btn, gpointer data)
{
    current_page = 0;
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "system");
}

static void on_keyboard_clicked(GtkButton *btn, gpointer data)
{
    current_page = 1;
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "keyboard");
}

static gboolean update_system(gpointer data)
{
    system_get_info(&sys_info);
    if (system_drawing_area)
        gtk_widget_queue_draw(system_drawing_area);
    return G_SOURCE_CONTINUE;
}

static void on_brightness_changed(GtkRange *range, gpointer data)
{
    int val = (int)gtk_range_get_value(range);
    kb_set_brightness(val);
}

static void on_power_profile_changed(GtkCheckButton *btn, gpointer data)
{
    if (!gtk_check_button_get_active(btn)) return;
    set_power_profile(GPOINTER_TO_INT(data));
}

static void on_fan_control_changed(GtkCheckButton *btn, gpointer data)
{
    if (!gtk_check_button_get_active(btn)) return;
    set_fan_control(GPOINTER_TO_INT(data));
}

static void on_led_mode_changed(GtkCheckButton *btn, gpointer data)
{
    if (!gtk_check_button_get_active(btn)) return;
    kb_set_led_mode(GPOINTER_TO_INT(data));
}

/* Create System page */
static void create_system_page(GtkWidget *parent)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    /* Title bar */
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_widget_set_margin_start(title_box, 20);
    gtk_widget_set_margin_top(title_box, 10);
    
    GtkWidget *title = gtk_label_new("System");
    gtk_widget_add_css_class(title, "page-title");
    gtk_box_append(GTK_BOX(title_box), title);
    
    GtkWidget *cpu_label = gtk_label_new("12th Gen Intel(R) Core(TM) i5-12500H");
    gtk_widget_add_css_class(cpu_label, "info-label");
    gtk_widget_set_margin_start(cpu_label, 20);
    gtk_box_append(GTK_BOX(title_box), cpu_label);
    
    gtk_box_append(GTK_BOX(box), title_box);
    
    /* Power mode selector */
    GtkWidget *power_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(power_box, 100);
    gtk_widget_set_margin_top(power_box, 10);
    
    const char *power_modes[] = {"Performance", "Entertainment", "Power Saving", "Quiet"};
    GtkWidget *first_power = NULL;
    int cur_power = get_power_profile();
    
    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_check_button_new_with_label(power_modes[i]);
        gtk_widget_add_css_class(btn, "mode-button");
        if (i == 0) first_power = btn;
        else gtk_check_button_set_group(GTK_CHECK_BUTTON(btn), GTK_CHECK_BUTTON(first_power));
        if (i == cur_power) gtk_check_button_set_active(GTK_CHECK_BUTTON(btn), TRUE);
        g_signal_connect(btn, "toggled", G_CALLBACK(on_power_profile_changed), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(power_box), btn);
    }
    gtk_box_append(GTK_BOX(box), power_box);
    
    /* Drawing area */
    system_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(system_drawing_area, TRUE);
    gtk_widget_set_hexpand(system_drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(system_drawing_area),
                                   system_draw, NULL, NULL);
    gtk_box_append(GTK_BOX(box), system_drawing_area);
    
    /* Fan control */
    GtkWidget *fan_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(fan_box, 100);
    gtk_widget_set_margin_bottom(fan_box, 15);
    
    GtkWidget *fan_label = gtk_label_new("FAN Speed:");
    gtk_box_append(GTK_BOX(fan_box), fan_label);
    
    const char *fan_modes[] = {"Automatic", "Maximum"};
    GtkWidget *first_fan = NULL;
    int cur_fan = get_fan_control();
    
    for (int i = 0; i < 2; i++) {
        GtkWidget *btn = gtk_check_button_new_with_label(fan_modes[i]);
        gtk_widget_add_css_class(btn, "mode-button");
        if (i == 0) first_fan = btn;
        else gtk_check_button_set_group(GTK_CHECK_BUTTON(btn), GTK_CHECK_BUTTON(first_fan));
        if (i == cur_fan) gtk_check_button_set_active(GTK_CHECK_BUTTON(btn), TRUE);
        g_signal_connect(btn, "toggled", G_CALLBACK(on_fan_control_changed), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(fan_box), btn);
    }
    gtk_box_append(GTK_BOX(box), fan_box);
    
    gtk_stack_add_named(GTK_STACK(parent), box, "system");
}

/* Create Keyboard page */
static void create_keyboard_page(GtkWidget *parent)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    /* Title */
    GtkWidget *title = gtk_label_new("LED Keyboard");
    gtk_widget_add_css_class(title, "page-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_margin_start(title, 20);
    gtk_box_append(GTK_BOX(box), title);
    
    /* Content */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_vexpand(content, TRUE);
    
    /* Left controls */
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(left_box, 20);
    gtk_widget_set_margin_top(left_box, 10);
    gtk_widget_set_size_request(left_box, 150, -1);
    
    /* LED Mode */
    GtkWidget *mode_label = gtk_label_new("Effect Mode");
    gtk_widget_add_css_class(mode_label, "section-title");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(left_box), mode_label);
    
    const char *led_modes[] = {"Static", "Wave", "Breath", "Blink"};
    GtkWidget *first_led = NULL;
    int cur_led = kb_get_led_mode();
    
    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_check_button_new_with_label(led_modes[i]);
        if (i == 0) first_led = btn;
        else gtk_check_button_set_group(GTK_CHECK_BUTTON(btn), GTK_CHECK_BUTTON(first_led));
        if (i == cur_led) gtk_check_button_set_active(GTK_CHECK_BUTTON(btn), TRUE);
        g_signal_connect(btn, "toggled", G_CALLBACK(on_led_mode_changed), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(left_box), btn);
    }
    
    gtk_box_append(GTK_BOX(content), left_box);
    
    /* Drawing area */
    keyboard_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(keyboard_drawing_area, TRUE);
    gtk_widget_set_hexpand(keyboard_drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(keyboard_drawing_area),
                                   keyboard_draw, NULL, NULL);
    gtk_box_append(GTK_BOX(content), keyboard_drawing_area);
    
    /* Right controls - Brightness */
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_end(right_box, 20);
    gtk_widget_set_size_request(right_box, 80, -1);
    
    GtkWidget *bright_label = gtk_label_new("Brightness");
    gtk_widget_add_css_class(bright_label, "section-title");
    gtk_box_append(GTK_BOX(right_box), bright_label);
    
    GtkWidget *bright_scale = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, 0, 9, 1);
    gtk_range_set_inverted(GTK_RANGE(bright_scale), TRUE);
    gtk_range_set_value(GTK_RANGE(bright_scale), kb_get_brightness());
    gtk_widget_set_size_request(bright_scale, -1, 150);
    g_signal_connect(bright_scale, "value-changed", G_CALLBACK(on_brightness_changed), NULL);
    gtk_box_append(GTK_BOX(right_box), bright_scale);
    
    gtk_box_append(GTK_BOX(content), right_box);
    gtk_box_append(GTK_BOX(box), content);
    
    gtk_stack_add_named(GTK_STACK(parent), box, "keyboard");
}

/* Main window */
static void on_activate(GtkApplication *app, gpointer data)
{
    /* Apply CSS */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, cc30_css, -1);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    /* Window */
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Control Center");
    gtk_window_set_default_size(GTK_WINDOW(window), 950, 650);
    
    /* Main layout */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    
    /* Sidebar */
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(sidebar, "sidebar");
    
    GtkWidget *sys_btn = gtk_toggle_button_new_with_label("⚙");
    GtkWidget *kb_btn = gtk_toggle_button_new_with_label("⌨");
    GtkWidget *flex_btn = gtk_toggle_button_new_with_label("F");
    GtkWidget *power_btn = gtk_toggle_button_new_with_label("⚡");
    
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sys_btn), TRUE);
    
    g_signal_connect(sys_btn, "clicked", G_CALLBACK(on_system_clicked), NULL);
    g_signal_connect(kb_btn, "clicked", G_CALLBACK(on_keyboard_clicked), NULL);
    
    gtk_box_append(GTK_BOX(sidebar), sys_btn);
    gtk_box_append(GTK_BOX(sidebar), kb_btn);
    gtk_box_append(GTK_BOX(sidebar), flex_btn);
    gtk_box_append(GTK_BOX(sidebar), power_btn);
    
    gtk_box_append(GTK_BOX(main_box), sidebar);
    
    /* Content stack */
    main_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(main_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(main_stack, TRUE);
    
    create_system_page(main_stack);
    create_keyboard_page(main_stack);
    
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "system");
    gtk_box_append(GTK_BOX(main_box), main_stack);
    
    gtk_window_set_child(GTK_WINDOW(window), main_box);
    
    /* Timer for updates */
    system_get_info(&sys_info);
    g_timeout_add(2000, update_system, NULL);
    
    gtk_window_present(GTK_WINDOW(window));
}

void ui_init(GtkApplication *app)
{
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
}
