/*
 * colorwheel.h - Color wheel widget for keyboard backlight
 */

#ifndef COLORWHEEL_H
#define COLORWHEEL_H

#include <gtk/gtk.h>

/* Draw a color wheel and return selected color at click position */
void draw_color_wheel(cairo_t *cr, double x, double y, double radius);

/* Get color at position (returns 0xRRGGBB) */
guint32 color_wheel_get_color(double x, double y, double wheel_x, double wheel_y, double radius);

/* Convert HSV to RGB */
void hsv_to_rgb(double h, double s, double v, double *r, double *g, double *b);

#endif /* COLORWHEEL_H */
