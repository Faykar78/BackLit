/*
 * gauges.c - Custom gauge widgets using Cairo
 * Implements CC30-style circular gauges and speedometer
 */

#include <cairo.h>
#include <math.h>
#include <stdio.h>
#include "gauges.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Draw a circular progress gauge */
void draw_circular_gauge(cairo_t *cr, double x, double y, double radius,
                         double value, double max_value, const char *label,
                         const char *sublabel)
{
    double fraction = max_value > 0 ? value / max_value : 0;
    if (fraction > 1.0) fraction = 1.0;
    
    double line_width = radius * 0.15;
    double inner_radius = radius - line_width;
    
    /* Background arc */
    cairo_set_line_width(cr, line_width);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_arc(cr, x, y, inner_radius, 0, 2 * M_PI);
    cairo_stroke(cr);
    
    /* Value arc (green) */
    cairo_set_source_rgb(cr, CC_GREEN);
    cairo_arc(cr, x, y, inner_radius, -M_PI/2, -M_PI/2 + fraction * 2 * M_PI);
    cairo_stroke(cr);
    
    /* Center text - value */
    cairo_set_source_rgb(cr, CC_GREEN);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, radius * 0.5);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", (int)value);
    
    cairo_text_extents_t ext;
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, x - ext.width/2, y + ext.height/4);
    cairo_show_text(cr, buf);
    
    /* Sublabel (below value) */
    if (sublabel) {
        cairo_set_source_rgb(cr, CC_WHITE);
        cairo_set_font_size(cr, radius * 0.2);
        cairo_text_extents(cr, sublabel, &ext);
        cairo_move_to(cr, x - ext.width/2, y + radius * 0.4);
        cairo_show_text(cr, sublabel);
    }
    
    /* Label (at top) */
    if (label) {
        cairo_set_source_rgb(cr, CC_WHITE);
        cairo_set_font_size(cr, radius * 0.2);
        cairo_text_extents(cr, label, &ext);
        cairo_move_to(cr, x - ext.width/2, y - radius * 0.3);
        cairo_show_text(cr, label);
    }
}

/* Draw fan gauge */
void draw_fan_gauge(cairo_t *cr, double x, double y, double radius,
                    int rpm, const char *label)
{
    double line_width = radius * 0.12;
    double inner_radius = radius - line_width;
    
    /* Background circle */
    cairo_set_line_width(cr, line_width);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_arc(cr, x, y, inner_radius, 0, 2 * M_PI);
    cairo_stroke(cr);
    
    /* Green arc based on RPM (assume max 5000 RPM) */
    double fraction = rpm > 0 ? (double)rpm / 5000.0 : 0;
    if (fraction > 1.0) fraction = 1.0;
    
    if (rpm > 0) {
        cairo_set_source_rgb(cr, CC_GREEN);
        cairo_arc(cr, x, y, inner_radius, -M_PI/2, -M_PI/2 + fraction * 2 * M_PI);
        cairo_stroke(cr);
    }
    
    /* Center value */
    cairo_set_source_rgb(cr, CC_GREEN);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, radius * 0.4);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", rpm);
    
    cairo_text_extents_t ext;
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, x - ext.width/2, y + ext.height/4);
    cairo_show_text(cr, buf);
    
    /* Label below */
    if (label) {
        cairo_set_source_rgb(cr, CC_WHITE);
        cairo_set_font_size(cr, radius * 0.18);
        cairo_text_extents(cr, label, &ext);
        cairo_move_to(cr, x - ext.width/2, y + radius * 0.55);
        cairo_show_text(cr, label);
    }
}

/* Draw CPU speedometer */
void draw_speedometer(cairo_t *cr, double x, double y, double radius,
                      double cpu_mhz, double utilization, double temperature)
{
    double line_width = radius * 0.04;
    double arc_radius = radius * 0.85;
    
    /* Draw outer arc (partial circle from 135° to 405°) */
    double start_angle = 135 * M_PI / 180;
    double end_angle = 405 * M_PI / 180;
    double arc_span = end_angle - start_angle;
    
    /* Background arc */
    cairo_set_line_width(cr, line_width);
    cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
    cairo_arc(cr, x, y, arc_radius, start_angle, end_angle);
    cairo_stroke(cr);
    
    /* Green arc based on utilization */
    double util_fraction = utilization / 100.0;
    if (util_fraction > 1.0) util_fraction = 1.0;
    
    cairo_set_source_rgb(cr, CC_GREEN);
    cairo_set_line_width(cr, line_width * 1.5);
    cairo_arc(cr, x, y, arc_radius, start_angle, start_angle + util_fraction * arc_span);
    cairo_stroke(cr);
    
    /* Draw tick marks (0-5) */
    cairo_set_source_rgb(cr, CC_WHITE);
    cairo_set_line_width(cr, 2);
    
    for (int i = 0; i <= 5; i++) {
        double tick_angle = start_angle + (i / 5.0) * arc_span;
        double inner_r = arc_radius * 0.75;
        double outer_r = arc_radius * 0.9;
        
        cairo_move_to(cr, x + cos(tick_angle) * inner_r, y + sin(tick_angle) * inner_r);
        cairo_line_to(cr, x + cos(tick_angle) * outer_r, y + sin(tick_angle) * outer_r);
        cairo_stroke(cr);
        
        /* Tick label */
        cairo_set_font_size(cr, radius * 0.1);
        char tick_buf[8];
        snprintf(tick_buf, sizeof(tick_buf), "%d", i);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, tick_buf, &ext);
        double label_r = arc_radius * 0.65;
        cairo_move_to(cr, x + cos(tick_angle) * label_r - ext.width/2,
                      y + sin(tick_angle) * label_r + ext.height/2);
        cairo_show_text(cr, tick_buf);
    }
    
    /* Draw needle */
    double needle_angle = start_angle + util_fraction * arc_span;
    cairo_set_source_rgb(cr, CC_GREEN);
    cairo_set_line_width(cr, 3);
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + cos(needle_angle) * arc_radius * 0.7,
                  y + sin(needle_angle) * arc_radius * 0.7);
    cairo_stroke(cr);
    
    /* Center circle */
    cairo_set_source_rgb(cr, CC_GREEN);
    cairo_arc(cr, x, y, radius * 0.08, 0, 2 * M_PI);
    cairo_fill(cr);
    
    /* CPU text in center */
    cairo_set_source_rgb(cr, CC_WHITE);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, radius * 0.15);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "CPU", &ext);
    cairo_move_to(cr, x - ext.width/2, y + radius * 0.3);
    cairo_show_text(cr, "CPU");
    
    /* Bottom stats */
    char buf[64];
    cairo_set_font_size(cr, radius * 0.12);
    
    /* MHz */
    snprintf(buf, sizeof(buf), "%.0f", cpu_mhz);
    cairo_set_source_rgb(cr, CC_WHITE);
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, x - radius * 0.6, y + radius * 0.85);
    cairo_show_text(cr, buf);
    
    cairo_set_font_size(cr, radius * 0.08);
    cairo_move_to(cr, x - radius * 0.6, y + radius * 0.95);
    cairo_show_text(cr, "MHz");
    
    /* Utilization */
    snprintf(buf, sizeof(buf), "%.0f%%", utilization);
    cairo_set_font_size(cr, radius * 0.12);
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, x - ext.width/2, y + radius * 0.85);
    cairo_show_text(cr, buf);
    
    cairo_set_font_size(cr, radius * 0.08);
    cairo_move_to(cr, x - radius * 0.1, y + radius * 0.95);
    cairo_show_text(cr, "Utilization");
    
    /* Temperature */
    snprintf(buf, sizeof(buf), "%.0f°C", temperature);
    cairo_set_source_rgb(cr, CC_GREEN);
    cairo_set_font_size(cr, radius * 0.15);
    cairo_text_extents(cr, buf, &ext);
    cairo_move_to(cr, x + radius * 0.3, y + radius * 0.85);
    cairo_show_text(cr, buf);
    
    cairo_set_source_rgb(cr, CC_WHITE);
    cairo_set_font_size(cr, radius * 0.08);
    cairo_move_to(cr, x + radius * 0.25, y + radius * 0.95);
    cairo_show_text(cr, "Temperature");
}
