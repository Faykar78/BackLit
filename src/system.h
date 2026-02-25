/*
 * system.h - System monitoring functions
 */

#ifndef SYSTEM_H
#define SYSTEM_H

typedef struct {
    float cpu_temp;
    float cpu_usage;
    int fan1_rpm;
    int fan2_rpm;
    float mem_usage;
    int bat_percent;
    int bat_charging;
} SystemInfo;

void system_get_info(SystemInfo *info);
float system_get_cpu_temp(void);
int system_get_fan_rpm(int idx);

#endif /* SYSTEM_H */
