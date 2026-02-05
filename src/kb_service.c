/*
 * kb_service.c - Background service for Keyboard Backlight Hotkeys
 * 
 * Monitors /dev/input/event* for Fn keys and updates led/sysfs
 * Runs as a lightweight background daemon (no GUI).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <signal.h>

#define SYSFS_PATH "/sys/devices/platform/clevo_xsm_wmi"

static volatile int running = 1;

/* Handle stats - similar to kb_gui.c but simpler */
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

static void write_sysfs(const char *attr, const char *value)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", SYSFS_PATH, attr);
    
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, value, strlen(value));
        close(fd);
    }
}

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

static void handle_toggle(void)
{
    char buf[128];
    static char saved_color[64] = "blue"; /* Default fallback */
    
    /* Check current color */
    if (read_sysfs("kb_color", buf, sizeof(buf)) < 0) return;
    
    char *first_color = strtok(buf, " ");
    
    if (first_color && strcmp(first_color, "black") == 0) {
        /* Is OFF, turn ON (restore saved) */
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "%s %s %s", saved_color, saved_color, saved_color);
        write_sysfs("kb_color", cmd);
        write_sysfs("kb_brightness", "0");
    } else {
        /* Is ON, save color and turn OFF */
        if (first_color) strncpy(saved_color, first_color, sizeof(saved_color)-1);
        write_sysfs("kb_color", "black");
    }
}

static void handle_brightness(int delta)
{
    char buf[16];
    if (read_sysfs("kb_brightness", buf, sizeof(buf)) < 0) return;
    
    int level = atoi(buf) + delta;
    if (level < 0) level = 0;
    if (level > 9) level = 9;
    
    snprintf(buf, sizeof(buf), "%d", level);
    write_sysfs("kb_brightness", buf);
}

void signal_handler(int signum) {
    running = 0;
}

int main(int argc, char *argv[])
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    char devpath[256];
    if (find_tuxedo_keyboard(devpath, sizeof(devpath)) < 0) {
        fprintf(stderr, "TUXEDO Keyboard not found\n");
        return 1;
    }
    
    printf("Starting kb_service on %s\n", devpath);
    int fd = open(devpath, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    struct input_event ev;
    while (running) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != sizeof(ev)) continue;
        
        if (ev.type == EV_KEY && ev.value == 1) { // Key press
            switch (ev.code) {
                case 228: handle_toggle(); break;     // KEY_KBDILLUMTOGGLE
                case 229: handle_brightness(1); break; // KEY_KBDILLUMDOWN
                case 230: handle_brightness(-1); break;// KEY_KBDILLUMUP
            }
        }
    }
    
    close(fd);
    return 0;
}
