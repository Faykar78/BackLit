/*
 * keyboard.h - Keyboard backlight control via sysfs
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#define SYSFS_PATH "/sys/devices/platform/clevo_xsm_wmi"

/* Available colors */
typedef struct {
    const char *name;
    const char *value;
    int r, g, b;
} KbColor;

extern const KbColor kb_colors[];
extern const int kb_num_colors;

/* Functions */
int kb_is_available(void);
int kb_get_brightness(void);
int kb_set_brightness(int level);
int kb_get_state(void);
int kb_set_state(int on);
char *kb_get_color(void);
int kb_set_color(const char *color);
int kb_get_wave(void);
int kb_set_wave(int on);
int kb_get_led_mode(void);
int kb_set_led_mode(int mode);
int get_fan_control(void);
int set_fan_control(int mode);
int get_power_profile(void);
int set_power_profile(int profile);

#endif /* KEYBOARD_H */
