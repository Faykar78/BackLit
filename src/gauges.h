/*
 * gauges.h - Custom gauge widgets using Cairo
 */

#ifndef GAUGES_H
#define GAUGES_H

#include <gtk/gtk.h>

/* Draw a circular progress gauge (like memory/storage gauges) */
void draw_circular_gauge(cairo_t *cr, double x, double y, double radius,
                         double value, double max_value, const char *label,
                         const char *sublabel);

/* Draw a speedometer-style CPU gauge */
void draw_speedometer(cairo_t *cr, double x, double y, double radius,
                      double cpu_mhz, double utilization, double temperature);

/* Draw a fan speed circular gauge */
void draw_fan_gauge(cairo_t *cr, double x, double y, double radius,
                    int rpm, const char *label);

/* Colors */
#define CC_GREEN   0.5, 1.0, 0.0   /* #7fff00 lime green */
#define CC_DARK    0.1, 0.1, 0.1   /* #1a1a1a dark bg */
#define CC_PANEL   0.15, 0.15, 0.15 /* #262626 panel bg */
#define CC_WHITE   1.0, 1.0, 1.0   /* white text */
#define CC_GRAY    0.5, 0.5, 0.5   /* gray */

#endif /* GAUGES_H */
