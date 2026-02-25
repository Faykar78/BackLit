/*
 * colorwheel.c - Color wheel widget for keyboard backlight
 */

#include <cairo.h>
#include <math.h>
#include "colorwheel.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void hsv_to_rgb(double h, double s, double v, double *r, double *g, double *b)
{
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    
    h = fmod(h, 360.0);
    if (h < 0) h += 360.0;
    
    double c = v * s;
    double x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
    double m = v - c;
    
    double r1, g1, b1;
    
    if (h < 60)       { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else              { r1 = c; g1 = 0; b1 = x; }
    
    *r = r1 + m;
    *g = g1 + m;
    *b = b1 + m;
}

void draw_color_wheel(cairo_t *cr, double x, double y, double radius)
{
    double inner_radius = radius * 0.6;
    double ring_width = radius - inner_radius;
    
    /* Draw the color ring */
    for (int angle = 0; angle < 360; angle++) {
        double rad = angle * M_PI / 180.0;
        double next_rad = (angle + 1) * M_PI / 180.0;
        
        double r, g, b;
        hsv_to_rgb(angle, 1.0, 1.0, &r, &g, &b);
        
        cairo_set_source_rgb(cr, r, g, b);
        
        /* Draw arc segment */
        cairo_move_to(cr, x + cos(rad) * inner_radius, y + sin(rad) * inner_radius);
        cairo_line_to(cr, x + cos(rad) * radius, y + sin(rad) * radius);
        cairo_arc(cr, x, y, radius, rad, next_rad);
        cairo_line_to(cr, x + cos(next_rad) * inner_radius, y + sin(next_rad) * inner_radius);
        cairo_arc_negative(cr, x, y, inner_radius, next_rad, rad);
        cairo_close_path(cr);
        cairo_fill(cr);
    }
    
    /* Draw center circle (dark) */
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_arc(cr, x, y, inner_radius - 2, 0, 2 * M_PI);
    cairo_fill(cr);
    
    /* Draw "Color backlight" text in center */
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, radius * 0.12);
    
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "Color backlight", &ext);
    cairo_move_to(cr, x - ext.width/2, y + ext.height/2);
    cairo_show_text(cr, "Color backlight");
}

guint32 color_wheel_get_color(double click_x, double click_y,
                              double wheel_x, double wheel_y, double radius)
{
    double dx = click_x - wheel_x;
    double dy = click_y - wheel_y;
    double dist = sqrt(dx*dx + dy*dy);
    
    double inner_radius = radius * 0.6;
    
    /* Check if click is in the color ring */
    if (dist >= inner_radius && dist <= radius) {
        double angle = atan2(dy, dx) * 180.0 / M_PI;
        if (angle < 0) angle += 360.0;
        
        double r, g, b;
        hsv_to_rgb(angle, 1.0, 1.0, &r, &g, &b);
        
        guint32 ir = (guint32)(r * 255);
        guint32 ig = (guint32)(g * 255);
        guint32 ib = (guint32)(b * 255);
        
        return (ir << 16) | (ig << 8) | ib;
    }
    
    return 0;  /* Not in ring */
}
