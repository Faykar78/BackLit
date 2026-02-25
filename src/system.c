/*
 * system.c - System monitoring functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include "system.h"

static float read_file_float(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    float val = 0;
    if (fscanf(f, "%f", &val) != 1) val = -1;
    fclose(f);
    return val;
}

static int read_file_int(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    int val = 0;
    if (fscanf(f, "%d", &val) != 1) val = -1;
    fclose(f);
    return val;
}

float system_get_cpu_temp(void)
{
    /* Try common hwmon temperature sources */
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return -1;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", entry->d_name);
        
        FILE *f = fopen(path, "r");
        if (!f) continue;
        
        char name[32];
        if (fscanf(f, "%31s", name) != 1) {
            fclose(f);
            continue;
        }
        fclose(f);
        
        /* Look for coretemp or k10temp */
        if (strcmp(name, "coretemp") == 0 || strcmp(name, "k10temp") == 0) {
            snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp1_input", entry->d_name);
            int temp = read_file_int(path);
            closedir(dir);
            return temp > 0 ? temp / 1000.0 : -1;
        }
    }
    closedir(dir);
    return -1;
}

int system_get_fan_rpm(int idx)
{
    /* Look for clevo hwmon or generic fan */
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return -1;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/hwmon/%s/fan%d_input", entry->d_name, idx + 1);
        
        int rpm = read_file_int(path);
        if (rpm >= 0) {
            closedir(dir);
            return rpm;
        }
    }
    closedir(dir);
    return -1;
}

static float get_cpu_usage(void)
{
    static long long prev_total = 0, prev_idle = 0;
    
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    
    long long user, nice, sys, idle, iowait, irq, softirq;
    if (fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld",
               &user, &nice, &sys, &idle, &iowait, &irq, &softirq) != 7) {
        fclose(f);
        return -1;
    }
    fclose(f);
    
    long long total = user + nice + sys + idle + iowait + irq + softirq;
    
    float usage = 0;
    if (prev_total > 0) {
        long long diff_total = total - prev_total;
        long long diff_idle = idle - prev_idle;
        if (diff_total > 0)
            usage = 100.0 * (diff_total - diff_idle) / diff_total;
    }
    
    prev_total = total;
    prev_idle = idle;
    return usage;
}

static float get_mem_usage(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    
    long total = 0, available = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, "%ld", &total);
        else if (strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, "%ld", &available);
    }
    fclose(f);
    
    if (total > 0)
        return 100.0 * (total - available) / total;
    return -1;
}

static void get_battery_info(int *percent, int *charging)
{
    *percent = -1;
    *charging = 0;
    
    /* Try common battery path */
    const char *bat_path = "/sys/class/power_supply/BAT0";
    char path[256];
    
    snprintf(path, sizeof(path), "%s/capacity", bat_path);
    *percent = read_file_int(path);
    
    snprintf(path, sizeof(path), "%s/status", bat_path);
    FILE *f = fopen(path, "r");
    if (f) {
        char status[32];
        if (fscanf(f, "%31s", status) == 1) {
            *charging = (strcmp(status, "Charging") == 0);
        }
        fclose(f);
    }
}

void system_get_info(SystemInfo *info)
{
    info->cpu_temp = system_get_cpu_temp();
    info->cpu_usage = get_cpu_usage();
    info->fan1_rpm = system_get_fan_rpm(0);
    info->fan2_rpm = system_get_fan_rpm(1);
    info->mem_usage = get_mem_usage();
    get_battery_info(&info->bat_percent, &info->bat_charging);
}
