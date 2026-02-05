/*
 * kb_ctl.c - Keyboard Backlight Control CLI
 * 
 * Direct EC control via sysfs interface
 * Uses same WMI commands as clevo_xsm_wmi kernel module
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#define SYSFS_PATH "/sys/devices/platform/clevo_xsm_wmi"

/* Color definitions - matches kernel module */
typedef struct {
    const char *name;
    const char *value;
    int r, g, b;
} KbColor;

static const KbColor kb_colors[] = {
    {"black",   "black",   0,   0,   0},
    {"blue",    "blue",    0,   0,   255},
    {"red",     "red",     255, 0,   0},
    {"magenta", "magenta", 255, 0,   255},
    {"green",   "green",   0,   255, 0},
    {"cyan",    "cyan",    0,   255, 255},
    {"yellow",  "yellow",  255, 255, 0},
    {"white",   "white",   255, 255, 255},
    {"orange",  "orange",  255, 128, 0},
    {"purple",  "purple",  128, 0,   255},
    {"pink",    "pink",    255, 0,   128},
    {"teal",    "teal",    0,   128, 128},
    {"lime",    "lime",    128, 255, 0},
};
#define NUM_COLORS (sizeof(kb_colors) / sizeof(kb_colors[0]))

/* Read from sysfs */
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

/* Write to sysfs using sudo tee - same format as:
 * echo VALUE | sudo tee /sys/devices/platform/clevo_xsm_wmi/ATTR
 */
static int write_sysfs(const char *attr, const char *value)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo '%s' | sudo tee %s/%s > /dev/null", 
             value, SYSFS_PATH, attr);
    return system(cmd);
}

/* Check if keyboard control is available */
static int kb_is_available(void)
{
    return access(SYSFS_PATH, F_OK) == 0;
}

/* Get functions */
static int kb_get_brightness(void)
{
    char buf[16];
    if (read_sysfs("kb_brightness", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

static int kb_get_state(void)
{
    char buf[16];
    if (read_sysfs("kb_state", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

static const char *kb_get_color(void)
{
    static char buf[64];
    if (read_sysfs("kb_color", buf, sizeof(buf)) < 0) return "unknown";
    return buf;
}

static int kb_get_wave(void)
{
    char buf[16];
    if (read_sysfs("kb_wave", buf, sizeof(buf)) < 0) return 0;
    return atoi(buf);
}

/* Set functions */
static int kb_set_brightness(int level)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", level);
    return write_sysfs("kb_brightness", buf);
}

static int kb_set_state(int on)
{
    return write_sysfs("kb_state", on ? "1" : "0");
}

static int kb_set_color(const char *color)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s %s", color, color, color);
    return write_sysfs("kb_color", buf);
}

static int kb_set_wave(int on)
{
    return write_sysfs("kb_wave", on ? "1" : "0");
}

/* Find color by name */
static int find_color(const char *name)
{
    for (size_t i = 0; i < NUM_COLORS; i++) {
        if (strcasecmp(name, kb_colors[i].name) == 0)
            return (int)i;
    }
    return -1;
}

/* Print status */
static void print_status(void)
{
    int state = kb_get_state();
    int brightness = kb_get_brightness();
    const char *color = kb_get_color();
    int wave = kb_get_wave();
    
    printf("╔═══════════════════════════════════════╗\n");
    printf("║     Keyboard Backlight Status         ║\n");
    printf("╠═══════════════════════════════════════╣\n");
    printf("║  State:      %-24s ║\n", state ? "ON" : "OFF");
    printf("║  Brightness: %-24d ║\n", brightness);
    printf("║  Color:      %-24s ║\n", color);
    printf("║  Wave:       %-24s ║\n", wave ? "Enabled" : "Disabled");
    printf("╚═══════════════════════════════════════╝\n");
}

/* Print help */
static void print_help(const char *prog)
{
    printf("Keyboard Backlight Control - EC Level\n\n");
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  -t, --toggle           Toggle backlight on/off\n");
    printf("  -o, --on               Turn backlight on\n");
    printf("  -O, --off              Turn backlight off\n");
    printf("  -b, --brightness N     Set brightness (0-9, 0=brightest)\n");
    printf("  -c, --color COLOR      Set color (blue, red, green, etc.)\n");
    printf("  -w, --wave             Enable wave effect\n");
    printf("  -W, --no-wave          Disable wave effect\n");
    printf("  -s, --status           Show current status\n");
    printf("  -h, --help             Show this help\n");
    printf("\nColors: ");
    for (size_t i = 0; i < NUM_COLORS; i++) {
        printf("%s%s", kb_colors[i].name, (i < NUM_COLORS - 1) ? ", " : "\n");
    }
}

int main(int argc, char *argv[])
{
    static struct option long_options[] = {
        {"toggle",     no_argument,       0, 't'},
        {"on",         no_argument,       0, 'o'},
        {"off",        no_argument,       0, 'O'},
        {"brightness", required_argument, 0, 'b'},
        {"color",      required_argument, 0, 'c'},
        {"wave",       no_argument,       0, 'w'},
        {"no-wave",    no_argument,       0, 'W'},
        {"status",     no_argument,       0, 's'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    if (!kb_is_available()) {
        fprintf(stderr, "Error: Keyboard backlight not available\n");
        fprintf(stderr, "Make sure clevo_xsm_wmi module is loaded\n");
        return 1;
    }
    
    /* No args = show status */
    if (argc == 1) {
        print_status();
        return 0;
    }
    
    int opt;
    while ((opt = getopt_long(argc, argv, "toOb:c:wWsh", long_options, NULL)) != -1) {
        switch (opt) {
        case 't': /* Toggle */
            {
                int current = kb_get_state();
                kb_set_state(!current);
                printf("Backlight: %s\n", !current ? "ON" : "OFF");
            }
            break;
            
        case 'o': /* On */
            kb_set_state(1);
            printf("Backlight: ON\n");
            break;
            
        case 'O': /* Off */
            kb_set_state(0);
            printf("Backlight: OFF\n");
            break;
            
        case 'b': /* Brightness */
            {
                int level = atoi(optarg);
                if (level < 0 || level > 9) {
                    fprintf(stderr, "Error: Brightness must be 0-9\n");
                    return 1;
                }
                kb_set_brightness(level);
                printf("Brightness: %d\n", level);
            }
            break;
            
        case 'c': /* Color */
            if (find_color(optarg) < 0) {
                fprintf(stderr, "Error: Unknown color '%s'\n", optarg);
                return 1;
            }
            kb_set_color(optarg);
            printf("Color: %s\n", optarg);
            break;
            
        case 'w': /* Wave on */
            kb_set_wave(1);
            printf("Wave: Enabled\n");
            break;
            
        case 'W': /* Wave off */
            kb_set_wave(0);
            printf("Wave: Disabled\n");
            break;
            
        case 's': /* Status */
            print_status();
            break;
            
        case 'h': /* Help */
            print_help(argv[0]);
            return 0;
            
        default:
            print_help(argv[0]);
            return 1;
        }
    }
    
    return 0;
}
