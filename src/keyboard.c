/*
 * keyboard.c - Keyboard backlight control via sysfs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "keyboard.h"

const KbColor kb_colors[] = {
    {"Blue",    "blue",    0,   0,   255},
    {"Cyan",    "cyan",    0,   255, 255},
    {"Green",   "green",   0,   255, 0},
    {"Lime",    "lime",    128, 255, 0},
    {"Yellow",  "yellow",  255, 255, 0},
    {"Orange",  "orange",  255, 128, 0},
    {"Red",     "red",     255, 0,   0},
    {"Pink",    "pink",    255, 0,   128},
    {"Magenta", "magenta", 255, 0,   255},
    {"Purple",  "purple",  128, 0,   255},
    {"Teal",    "teal",    0,   128, 128},
    {"White",   "white",   255, 255, 255},
};
const int kb_num_colors = sizeof(kb_colors) / sizeof(kb_colors[0]);

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
    
    /* Remove trailing newline */
    if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';
    
    return 0;
}

static int write_sysfs(const char *attr, const char *value)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", SYSFS_PATH, attr);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        /* Try with sudo via system() - not ideal but works */
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "echo '%s' | sudo tee %s > /dev/null", value, path);
        return system(cmd);
    }
    
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    
    return n < 0 ? -1 : 0;
}

int kb_is_available(void)
{
    return access(SYSFS_PATH, F_OK) == 0;
}

int kb_get_brightness(void)
{
    char buf[16];
    if (read_sysfs("kb_brightness", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

int kb_set_brightness(int level)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", level);
    return write_sysfs("kb_brightness", buf);
}

int kb_get_state(void)
{
    char buf[16];
    if (read_sysfs("kb_state", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

int kb_set_state(int on)
{
    return write_sysfs("kb_state", on ? "1" : "0");
}

char *kb_get_color(void)
{
    static char buf[64];
    if (read_sysfs("kb_color", buf, sizeof(buf)) < 0) return "unknown";
    return buf;
}

int kb_set_color(const char *color)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s %s", color, color, color);
    return write_sysfs("kb_color", buf);
}

int kb_get_wave(void)
{
    char buf[16];
    if (read_sysfs("kb_wave", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

int kb_set_wave(int on)
{
    return write_sysfs("kb_wave", on ? "1" : "0");
}

/* LED Mode: 0=static, 1=wave, 2=breath, 3=blink */
int kb_get_led_mode(void)
{
    char buf[32];
    if (read_sysfs("kb_mode", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

int kb_set_led_mode(int mode)
{
    const char *modes[] = {"static", "wave", "breath", "blink"};
    if (mode < 0 || mode > 3) mode = 0;
    return write_sysfs("kb_mode", modes[mode]);
}

/* Fan Control: 0=auto, 1=max, 2=custom */
int get_fan_control(void)
{
    char buf[32];
    if (read_sysfs("fan_control", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

int set_fan_control(int mode)
{
    const char *modes[] = {"auto", "max", "custom"};
    if (mode < 0 || mode > 2) mode = 0;
    return write_sysfs("fan_control", modes[mode]);
}

/* Power Profile: 0=performance, 1=entertainment, 2=power_saving, 3=quiet */
int get_power_profile(void)
{
    char buf[32];
    if (read_sysfs("power_profile", buf, sizeof(buf)) < 0) return 2;
    return atoi(buf);
}

int set_power_profile(int profile)
{
    const char *profiles[] = {"performance", "entertainment", "power_saving", "quiet"};
    if (profile < 0 || profile > 3) profile = 2;
    return write_sysfs("power_profile", profiles[profile]);
}
