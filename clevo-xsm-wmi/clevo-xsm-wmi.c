/*
 * clevo-xsm-wmi.c
 *
 * Copyright (C) 2014-2016 Arnoud Willemsen <mail@lynthium.com>
 *
 * Based on tuxedo-wmi by TUXEDO Computers GmbH
 * Copyright (C) 2013-2015 TUXEDO Computers GmbH <tux@tuxedocomputers.com>
 * Custom build Linux Notebooks and Computers: www.tuxedocomputers.com
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the  GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is  distributed in the hope that it  will be useful, but
 * WITHOUT  ANY   WARRANTY;  without   even  the  implied   warranty  of
 * MERCHANTABILITY  or FITNESS FOR  A PARTICULAR  PURPOSE.  See  the GNU
 * General Public License for more details.
 *
 * You should  have received  a copy of  the GNU General  Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define CLEVO_XSM_DRIVER_NAME KBUILD_MODNAME
#define pr_fmt(fmt) CLEVO_XSM_DRIVER_NAME ": " fmt

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/stringify.h>
#include <linux/version.h>
#include <linux/workqueue.h>

#define __CLEVO_XSM_PR(lvl, fmt, ...) do { pr_##lvl(fmt, ##__VA_ARGS__); } \
		while (0)
#define CLEVO_XSM_INFO(fmt, ...) __CLEVO_XSM_PR(info, fmt, ##__VA_ARGS__)
#define CLEVO_XSM_ERROR(fmt, ...) __CLEVO_XSM_PR(err, fmt, ##__VA_ARGS__)
#define CLEVO_XSM_DEBUG(fmt, ...) __CLEVO_XSM_PR(debug, "[%s:%u] " fmt, \
		__func__, __LINE__, ##__VA_ARGS__)

#define CLEVO_EVENT_GUID  "ABBC0F6B-8EA1-11D1-00A0-C90629100000"
#define CLEVO_EMAIL_GUID  "ABBC0F6C-8EA1-11D1-00A0-C90629100000"
#define CLEVO_GET_GUID    "ABBC0F6D-8EA1-11D1-00A0-C90629100000"

#define CLEVO_HAS_HWMON (defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE)))

/* method IDs for CLEVO_GET */
#define GET_EVENT               0x01  /*   1 */
#define GET_POWER_STATE_FOR_3G  0x0A  /*  10 */
#define GET_AP                  0x46  /*  70 */
#define SET_3G                  0x4C  /*  76 */
#define SET_KB_LED              0x67  /* 103 */
#define AIRPLANE_BUTTON         0x6D  /* 109 */    /* or 0x6C (?) */
#define TALK_BIOS_3G            0x78  /* 120 */

#define COLORS { C(black,  0x000000), C(blue,    0x0000FF), \
				 C(red,    0xFF0000), C(magenta, 0xFF00FF), \
				 C(green,  0x00FF00), C(cyan,    0x00FFFF), \
				 C(yellow, 0xFFFF00), C(white,   0xFFFFFF), \
				 C(orange, 0xFF8000), C(purple,  0x8000FF), \
				 C(pink,   0xFF0080), C(teal,    0x008080), \
				 C(lime,   0x80FF00), }
#undef C

#define C(n, v) KB_COLOR_##n
enum kb_color COLORS;
#undef C

union kb_rgb_color {
	u32 rgb;
	struct { u32 b:8, g:8, r:8, : 8; };
};

#define C(n, v) { .name = #n, .value = { .rgb = v, }, }
struct {
	const char *const name;
	union kb_rgb_color value;
} kb_colors[] = COLORS;
#undef C

#define KB_COLOR_DEFAULT      KB_COLOR_blue
#define KB_BRIGHTNESS_MAX     10
#define KB_BRIGHTNESS_DEFAULT KB_BRIGHTNESS_MAX

static int param_set_kb_color(const char *val, const struct kernel_param *kp)
{
	size_t i;

	if (!val)
		return -EINVAL;

	if (!val[0]) {
		*((enum kb_color *) kp->arg) = KB_COLOR_black;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(kb_colors); i++) {
		if (!strcmp(val, kb_colors[i].name)) {
			*((enum kb_color *) kp->arg) = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int param_get_kb_color(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%s",
		kb_colors[*((enum kb_color *) kp->arg)].name);
}

static const struct kernel_param_ops param_ops_kb_color = {
	.set = param_set_kb_color,
	.get = param_get_kb_color,
};

static enum kb_color param_kb_color[] = { [0 ... 3] = KB_COLOR_DEFAULT };
static int param_kb_color_num;
#define param_check_kb_color(name, p) __param_check(name, p, enum kb_color)
module_param_array_named(kb_color, param_kb_color, kb_color,
						 &param_kb_color_num, S_IRUSR);
MODULE_PARM_DESC(kb_color, "Set the color(s) of the keyboard (sections)");


static int param_set_kb_brightness(const char *val,
	const struct kernel_param *kp)
{
	int ret;

	ret = param_set_byte(val, kp);

	if (!ret && *((unsigned char *) kp->arg) > KB_BRIGHTNESS_MAX)
		return -EINVAL;

	return ret;
}

static const struct kernel_param_ops param_ops_kb_brightness = {
	.set = param_set_kb_brightness,
	.get = param_get_byte,
};

static unsigned char param_kb_brightness = KB_BRIGHTNESS_DEFAULT;
#define param_check_kb_brightness param_check_byte
module_param_named(kb_brightness, param_kb_brightness, kb_brightness, S_IRUSR);
MODULE_PARM_DESC(kb_brightness, "Set the brightness of the keyboard backlight");


static bool param_kb_off;
module_param_named(kb_off, param_kb_off, bool, S_IRUSR);
MODULE_PARM_DESC(kb_off, "Switch keyboard backlight off");

static bool param_kb_cycle_colors = true;
module_param_named(kb_cycle_colors, param_kb_cycle_colors, bool, S_IRUSR);
MODULE_PARM_DESC(kb_cycle_colors, "Cycle colors rather than modes");


#define POLL_FREQ_MIN     1
#define POLL_FREQ_MAX     20
#define POLL_FREQ_DEFAULT 5

static int param_set_poll_freq(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_byte(val, kp);

	if (!ret)
		*((unsigned char *) kp->arg) = clamp_t(unsigned char,
			*((unsigned char *) kp->arg),
			POLL_FREQ_MIN, POLL_FREQ_MAX);

	return ret;
}


static const struct kernel_param_ops param_ops_poll_freq = {
	.set = param_set_poll_freq,
	.get = param_get_byte,
};

static unsigned char param_poll_freq = POLL_FREQ_DEFAULT;
#define param_check_poll_freq param_check_byte
module_param_named(poll_freq, param_poll_freq, poll_freq, S_IRUSR);
MODULE_PARM_DESC(poll_freq, "Set polling frequency");


struct platform_device *clevo_xsm_platform_device;


/* LED sub-driver */

static bool param_led_invert;
module_param_named(led_invert, param_led_invert, bool, 0);
MODULE_PARM_DESC(led_invert, "Invert airplane mode LED state.");

static struct workqueue_struct *led_workqueue;

static struct _led_work {
	struct work_struct work;
	int wk;
} led_work;

static void airplane_led_update(struct work_struct *work)
{
	u8 byte;
	struct _led_work *w;

	w = container_of(work, struct _led_work, work);

	ec_read(0xD9, &byte);

	if (param_led_invert)
		ec_write(0xD9, w->wk ? byte & ~0x40 : byte | 0x40);
	else
		ec_write(0xD9, w->wk ? byte | 0x40 : byte & ~0x40);

	/* wmbb 0x6C 1 (?) */
}

static enum led_brightness airplane_led_get(struct led_classdev *led_cdev)
{
	u8 byte;

	ec_read(0xD9, &byte);

	if (param_led_invert)
		return byte & 0x40 ? LED_OFF : LED_FULL;
	else
		return byte & 0x40 ? LED_FULL : LED_OFF;
}

/* must not sleep */
static void airplane_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	led_work.wk = value;
	queue_work(led_workqueue, &led_work.work);
}

static struct led_classdev airplane_led = {
	.name = "clevo_xsm::airplane",
	.brightness_get = airplane_led_get,
	.brightness_set = airplane_led_set,
	.max_brightness = 1,
};

static int __init clevo_xsm_led_init(void)
{
	int err;

	led_workqueue = create_singlethread_workqueue("led_workqueue");
	if (unlikely(!led_workqueue))
		return -ENOMEM;

	INIT_WORK(&led_work.work, airplane_led_update);

	err = led_classdev_register(&clevo_xsm_platform_device->dev,
		&airplane_led);
	if (unlikely(err))
		goto err_destroy_workqueue;

	return 0;

err_destroy_workqueue:
	destroy_workqueue(led_workqueue);
	led_workqueue = NULL;

	return err;
}

static void __exit clevo_xsm_led_exit(void)
{
	if (!IS_ERR_OR_NULL(airplane_led.dev))
		led_classdev_unregister(&airplane_led);
	if (led_workqueue)
		destroy_workqueue(led_workqueue);
}

/* Kernel-space wave animation - uses direct WMI for minimal latency */
static struct delayed_work wave_work;
static struct workqueue_struct *wave_workqueue;
static bool wave_running = false;
static unsigned int wave_step = 0;
static unsigned int wave_color_idx = 0;
static unsigned int wave_interval_ms = 40;
module_param(wave_interval_ms, uint, 0644);
MODULE_PARM_DESC(wave_interval_ms, "Wave animation step interval in ms (default 40)");

static unsigned int wave_last_brightness = 99;

/* Forward declaration */
static int clevo_xsm_wmi_evaluate_wmbb_method(u32 method_id, u32 arg, u32 *retval);

/* Color values for wave effect */
static const u32 wave_color_values[] = {
	0x0000FF, /* blue */
	0x00FFFF, /* cyan */
	0x00FF00, /* green */
	0xFFFF00, /* yellow */
	0xFF8000, /* orange */
	0xFF0000, /* red */
	0xFF0080, /* pink */
	0xFF00FF, /* magenta */
	0x8000FF, /* purple */
	0x008080, /* teal */
	0xFFFFFF, /* white */
};
#define WAVE_NUM_COLORS 11

/* Sine table for brightness 0-9 (0=bright, 9=dim) */
static const u8 sine_table[] = {9, 8, 7, 5, 3, 1, 0, 1, 3, 5, 7, 8, 9};
#define WAVE_TABLE_SIZE 13
#define NUM_WAVE_STEPS 19

static void wave_set_brightness_direct(unsigned int level)
{
	u8 raw = 0xFF - (level * 0x19);
	clevo_xsm_wmi_evaluate_wmbb_method(0x67, 0xF4000000 | raw, NULL);
}

static void wave_set_color_direct(unsigned int idx)
{
	u32 color = wave_color_values[idx % WAVE_NUM_COLORS];
	/* Color format: B << 16 | R << 8 | G << 0 */
	u8 r = (color >> 16) & 0xFF;
	u8 g = (color >> 8) & 0xFF;
	u8 b = color & 0xFF;
	u32 cmd_val = (b << 16) | (r << 8) | g;
	
	/* Set all 3 zones: left (F0), center (F1), right (F2) */
	clevo_xsm_wmi_evaluate_wmbb_method(0x67, 0xF0000000 | cmd_val, NULL);
	clevo_xsm_wmi_evaluate_wmbb_method(0x67, 0xF1000000 | cmd_val, NULL);
	clevo_xsm_wmi_evaluate_wmbb_method(0x67, 0xF2000000 | cmd_val, NULL);
}

static void wave_work_handler(struct work_struct *work)
{
	/* Smooth wave with all 10 brightness levels */
	/* 0=bright, 9=dim */
	static const u8 brightness_levels[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
	
	if (!wave_running)
		return;
	
	/* Set brightness */
	wave_set_brightness_direct(brightness_levels[wave_step]);
	
	/* Change color at step 9 (brightness 9 = dimmest) */
	if (wave_step == 9) {
		wave_color_idx = (wave_color_idx + 1) % WAVE_NUM_COLORS;
		wave_set_color_direct(wave_color_idx);
	}
	
	wave_step++;
	if (wave_step >= NUM_WAVE_STEPS)
		wave_step = 0;
	
	if (wave_running)
		queue_delayed_work(wave_workqueue, &wave_work,
			msecs_to_jiffies(wave_interval_ms));
}

static void wave_start(void)
{
	if (wave_running)
		return;
	
	wave_running = true;
	wave_step = 0;
	wave_last_brightness = 99;
	
	/* Set max brightness (0 = max in inverted system) */
	wave_set_brightness_direct(0);
	
	if (!wave_workqueue)
		wave_workqueue = create_singlethread_workqueue("kb_wave_wq");
	
	if (wave_workqueue)
		queue_delayed_work(wave_workqueue, &wave_work, 0);
}

static void wave_stop(void)
{
	wave_running = false;
	
	if (wave_workqueue)
		cancel_delayed_work_sync(&wave_work);
	
	wave_set_brightness_direct(0);  /* 0 = max brightness */
}

/* input sub-driver */

static struct input_dev *clevo_xsm_input_device;
static DEFINE_MUTEX(clevo_xsm_input_report_mutex);

static unsigned int global_report_cnt;

/* call with clevo_xsm_input_report_mutex held */
static void clevo_xsm_input_report_key(unsigned int code)
{
	input_report_key(clevo_xsm_input_device, code, 1);
	input_report_key(clevo_xsm_input_device, code, 0);
	input_sync(clevo_xsm_input_device);

	global_report_cnt++;
}

static struct task_struct *clevo_xsm_input_polling_task;

static int clevo_xsm_input_polling_thread(void *data)
{
	unsigned int report_cnt = 0;

	CLEVO_XSM_INFO("Polling thread started (PID: %i), polling at %i Hz\n",
				current->pid, param_poll_freq);

	while (!kthread_should_stop()) {

		u8 byte;

		ec_read(0xDB, &byte);
		if (byte & 0x40) {
			ec_write(0xDB, byte & ~0x40);

			CLEVO_XSM_DEBUG("Airplane-Mode Hotkey pressed\n");

			mutex_lock(&clevo_xsm_input_report_mutex);

			if (global_report_cnt > report_cnt) {
				mutex_unlock(&clevo_xsm_input_report_mutex);
				break;
			}

			clevo_xsm_input_report_key(KEY_RFKILL);
			report_cnt++;

			CLEVO_XSM_DEBUG("Led status: %d",
				airplane_led_get(&airplane_led));

			airplane_led_set(&airplane_led,
				(airplane_led_get(&airplane_led) ? 0 : 1));

			mutex_unlock(&clevo_xsm_input_report_mutex);
		}
		msleep_interruptible(1000 / param_poll_freq);
	}

	CLEVO_XSM_INFO("Polling thread exiting\n");

	return 0;
}

static int clevo_xsm_input_open(struct input_dev *dev)
{
	clevo_xsm_input_polling_task = kthread_run(
		clevo_xsm_input_polling_thread,
		NULL, "clevo_xsm-polld");

	if (unlikely(IS_ERR(clevo_xsm_input_polling_task))) {
		clevo_xsm_input_polling_task = NULL;
		CLEVO_XSM_ERROR("Could not create polling thread\n");
				return PTR_ERR(clevo_xsm_input_polling_task);
	}

		return 0;
}

static void clevo_xsm_input_close(struct input_dev *dev)
{
	if (unlikely(IS_ERR_OR_NULL(clevo_xsm_input_polling_task)))
		return;

	kthread_stop(clevo_xsm_input_polling_task);
	clevo_xsm_input_polling_task = NULL;
}

static int __init clevo_xsm_input_init(void)
{
	int err;
	u8 byte;

	clevo_xsm_input_device = input_allocate_device();
	if (unlikely(!clevo_xsm_input_device)) {
		CLEVO_XSM_ERROR("Error allocating input device\n");
		return -ENOMEM;
	}

	clevo_xsm_input_device->name = "Clevo Airplane-Mode Hotkey";
	clevo_xsm_input_device->phys = CLEVO_XSM_DRIVER_NAME "/input0";
	clevo_xsm_input_device->id.bustype = BUS_HOST;
	clevo_xsm_input_device->dev.parent = &clevo_xsm_platform_device->dev;

	clevo_xsm_input_device->open  = clevo_xsm_input_open;
	clevo_xsm_input_device->close = clevo_xsm_input_close;

	set_bit(EV_KEY, clevo_xsm_input_device->evbit);
	set_bit(KEY_RFKILL, clevo_xsm_input_device->keybit);

	ec_read(0xDB, &byte);
	ec_write(0xDB, byte & ~0x40);

	err = input_register_device(clevo_xsm_input_device);
	if (unlikely(err)) {
		CLEVO_XSM_ERROR("Error registering input device\n");
		goto err_free_input_device;
	}

	return 0;

err_free_input_device:
		input_free_device(clevo_xsm_input_device);

		return err;
}

static void __exit clevo_xsm_input_exit(void)
{
	if (unlikely(!clevo_xsm_input_device))
		return;

	input_unregister_device(clevo_xsm_input_device);
		clevo_xsm_input_device = NULL;
}


static int clevo_xsm_wmi_evaluate_wmbb_method(u32 method_id, u32 arg,
	u32 *retval)
{
	struct acpi_buffer in  = { (acpi_size) sizeof(arg), &arg };
		struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
		union acpi_object *obj;
		acpi_status status;
	u32 tmp;

	CLEVO_XSM_DEBUG("%0#4x  IN : %0#6x\n", method_id, arg);

	status = wmi_evaluate_method(CLEVO_GET_GUID, 0x00,
		method_id, &in, &out);

	if (unlikely(ACPI_FAILURE(status)))
		goto exit;

	obj = (union acpi_object *) out.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER)
			tmp = (u32) obj->integer.value;
	else
			tmp = 0;

	CLEVO_XSM_DEBUG("%0#4x  OUT: %0#6x (IN: %0#6x)\n", method_id, tmp, arg);

	if (likely(retval))
			*retval = tmp;

	kfree(obj);

exit:
	if (unlikely(ACPI_FAILURE(status)))
		return -EIO;

	return 0;
}


static struct {
	enum kb_extra {
		KB_HAS_EXTRA_TRUE,
		KB_HAS_EXTRA_FALSE,
	} extra;

	enum kb_state {
		KB_STATE_OFF,
		KB_STATE_ON,
	} state;

	struct {
		unsigned left;
		unsigned center;
		unsigned right;
		unsigned extra;
	} color;

	unsigned brightness;

	enum kb_mode {
		KB_MODE_RANDOM_COLOR,
		KB_MODE_CUSTOM,
		KB_MODE_BREATHE,
		KB_MODE_CYCLE,
		KB_MODE_WAVE,
		KB_MODE_DANCE,
		KB_MODE_TEMPO,
		KB_MODE_FLASH,
	} mode;

	struct kb_backlight_ops {
		void (*set_state)(enum kb_state state);
		void (*set_color)(unsigned left, unsigned center,
			unsigned right, unsigned extra);
		void (*set_brightness)(unsigned brightness);
		void (*set_mode)(enum kb_mode);
		void (*init)(void);
	} *ops;

} kb_backlight = { .ops = NULL, };


static void kb_dec_brightness(void)
{
	if (kb_backlight.state == KB_STATE_OFF)
		return;
	if (kb_backlight.brightness == 0)
		return;

	CLEVO_XSM_DEBUG();

	kb_backlight.ops->set_brightness(kb_backlight.brightness - 1);
}

static void kb_inc_brightness(void)
{
	if (kb_backlight.state == KB_STATE_OFF)
		return;

	CLEVO_XSM_DEBUG();

	kb_backlight.ops->set_brightness(kb_backlight.brightness + 1);
}

static void kb_toggle_state(void)
{
	/* Static vars to save last colors before turning off */
	static unsigned saved_left = 1;   /* default cyan */
	static unsigned saved_center = 1;
	static unsigned saved_right = 1;
	static unsigned saved_extra = 1;
	
	switch (kb_backlight.state) {
	case KB_STATE_OFF:
		/* Turn ON: restore saved colors */
		kb_backlight.ops->set_color(saved_left, saved_center, 
					    saved_right, saved_extra);
		kb_backlight.ops->set_brightness(0); /* max brightness */
		kb_backlight.state = KB_STATE_ON;
		break;
	case KB_STATE_ON:
		/* Turn OFF: save current colors, set to black (index 0) */
		saved_left = kb_backlight.color.left;
		saved_center = kb_backlight.color.center;
		saved_right = kb_backlight.color.right;
		saved_extra = kb_backlight.color.extra;
		kb_backlight.ops->set_color(0, 0, 0, 0); /* black = off */
		kb_backlight.state = KB_STATE_OFF;
		break;
	default:
		BUG();
	}
}

static void kb_next_mode(void)
{
	static enum kb_mode modes[] = {
		KB_MODE_RANDOM_COLOR,
		KB_MODE_DANCE,
		KB_MODE_TEMPO,
		KB_MODE_FLASH,
		KB_MODE_WAVE,
		KB_MODE_BREATHE,
		KB_MODE_CYCLE,
		KB_MODE_CUSTOM,
	};

	size_t i;

	if (kb_backlight.state == KB_STATE_OFF)
		return;

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (modes[i] == kb_backlight.mode)
			break;
	}

	BUG_ON(i == ARRAY_SIZE(modes));

	kb_backlight.ops->set_mode(modes[(i + 1) % ARRAY_SIZE(modes)]);
}

static void kb_next_color(void)
{
	size_t i;
	unsigned int nc;

	if (kb_backlight.state == KB_STATE_OFF)
		return;

	for (i = 0; i < ARRAY_SIZE(kb_colors); i++) {
		if (i == kb_backlight.color.left)
			break;
	}

	if (i + 1 > ARRAY_SIZE(kb_colors))
		nc = 0;
	else
		nc = i + 1;

	kb_backlight.ops->set_color(nc, nc, nc, nc);
}

/* full color backlight keyboard */

static void kb_full_color__set_color(unsigned left, unsigned center,
	unsigned right, unsigned extra)
{
	u32 cmd;

	cmd = 0xF0000000;
	cmd |= kb_colors[left].value.b << 16;
	cmd |= kb_colors[left].value.r <<  8;
	cmd |= kb_colors[left].value.g <<  0;

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, cmd, NULL))
		kb_backlight.color.left = left;

	cmd = 0xF1000000;
	cmd |= kb_colors[center].value.b << 16;
	cmd |= kb_colors[center].value.r <<  8;
	cmd |= kb_colors[center].value.g <<  0;

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, cmd, NULL))
		kb_backlight.color.center = center;

	cmd = 0xF2000000;
	cmd |= kb_colors[right].value.b << 16;
	cmd |= kb_colors[right].value.r <<  8;
	cmd |= kb_colors[right].value.g <<  0;

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, cmd, NULL))
		kb_backlight.color.right = right;

	if (kb_backlight.extra == KB_HAS_EXTRA_TRUE) {
		cmd = 0xF3000000;
		cmd |= kb_colors[extra].value.b << 16;
		cmd |= kb_colors[extra].value.r << 8;
		cmd |= kb_colors[extra].value.g << 0;

		if(!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, cmd, NULL))
			kb_backlight.color.extra = extra;
	}

	kb_backlight.mode = KB_MODE_CUSTOM;
}

static void kb_full_color__set_brightness(unsigned i)
{
	/* Firmware-native 10 brightness levels (0-9)
	 * From DSDT: raw = 0xFF - (level * 0x19)
	 * Level 0 = 0xFF (max), Level 9 = 0x0E (min)
	 */
	u8 raw_brightness;
	
	i = clamp_t(unsigned, i, 0, 9);
	raw_brightness = 0xFF - (i * 0x19);  /* Match EC firmware formula */

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED,
		0xF4000000 | raw_brightness, NULL))
		kb_backlight.brightness = i;
}

static void kb_full_color__set_mode(unsigned mode)
{
	static u32 cmds[] = {
		[KB_MODE_BREATHE]      = 0x1002a000,
		[KB_MODE_CUSTOM]       = 0,
		[KB_MODE_CYCLE]        = 0x33010000,
		[KB_MODE_DANCE]        = 0x80000000,
		[KB_MODE_FLASH]        = 0xA0000000,
		[KB_MODE_RANDOM_COLOR] = 0x70000000,
		[KB_MODE_TEMPO]        = 0x90000000,
		[KB_MODE_WAVE]         = 0xB0000000,
	};

	BUG_ON(mode >= ARRAY_SIZE(cmds));

	clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, 0x10000000, NULL);

	if (mode == KB_MODE_CUSTOM) {
		kb_full_color__set_color(kb_backlight.color.left,
			kb_backlight.color.center,
			kb_backlight.color.right,
			kb_backlight.color.extra);
		kb_full_color__set_brightness(kb_backlight.brightness);
		return;
	}

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, cmds[mode], NULL))
		kb_backlight.mode = mode;
}

static void kb_full_color__set_state(enum kb_state state)
{
	u32 cmd = 0xE0000000;

	CLEVO_XSM_DEBUG("State: %d\n", state);

	switch (state) {
	case KB_STATE_OFF:
		cmd |= 0x003001;
		break;
	case KB_STATE_ON:
		cmd |= 0x07F001;
		break;
	default:
		BUG();
	}

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, cmd, NULL))
		kb_backlight.state = state;
}

static void kb_full_color__init(void)
{
	CLEVO_XSM_DEBUG();

	kb_backlight.extra = KB_HAS_EXTRA_FALSE;

	kb_full_color__set_state(param_kb_off ? KB_STATE_OFF : KB_STATE_ON);
	kb_full_color__set_color(param_kb_color[0], param_kb_color[1],
		param_kb_color[2], param_kb_color[3]);
	kb_full_color__set_brightness(param_kb_brightness);
}

static struct kb_backlight_ops kb_full_color_ops = {
	.set_state      = kb_full_color__set_state,
	.set_color      = kb_full_color__set_color,
	.set_brightness = kb_full_color__set_brightness,
	.set_mode       = kb_full_color__set_mode,
	.init           = kb_full_color__init,
};

static void kb_full_color__init_extra(void)
{
	CLEVO_XSM_DEBUG();

	kb_backlight.extra = KB_HAS_EXTRA_TRUE;

	kb_full_color__set_state(param_kb_off ? KB_STATE_OFF : KB_STATE_ON);
	kb_full_color__set_color(param_kb_color[0], param_kb_color[1],
		param_kb_color[2], param_kb_color[3]);
	kb_full_color__set_brightness(param_kb_brightness);
}

static struct kb_backlight_ops kb_full_color_with_extra_ops = {
	.set_state      = kb_full_color__set_state,
	.set_color      = kb_full_color__set_color,
	.set_brightness = kb_full_color__set_brightness,
	.set_mode       = kb_full_color__set_mode,
	.init           = kb_full_color__init_extra,
};

/* 8 color backlight keyboard */

static void kb_8_color__set_color(unsigned left, unsigned center,
	unsigned right, unsigned extra)
{
	u32 cmd = 0x02010000;

	cmd |= kb_backlight.brightness << 12;
	cmd |= right  << 8;
	cmd |= center << 4;
	cmd |= left;

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, cmd, NULL)) {
		kb_backlight.color.left   = left;
		kb_backlight.color.center = center;
		kb_backlight.color.right  = right;
	}

	kb_backlight.mode = KB_MODE_CUSTOM;
}

static void kb_8_color__set_brightness(unsigned i)
{
	u32 cmd = 0xD2010000;

	i = clamp_t(unsigned, i, 0, KB_BRIGHTNESS_MAX);

	cmd |= i << 12;
	cmd |= kb_backlight.color.right  << 8;
	cmd |= kb_backlight.color.center << 4;
	cmd |= kb_backlight.color.left;

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, cmd, NULL))
		kb_backlight.brightness = i;
}

static void kb_8_color__set_mode(unsigned mode)
{
	static u32 cmds[] = {
		[KB_MODE_BREATHE]      = 0x12010000,
		[KB_MODE_CUSTOM]       = 0,
		[KB_MODE_CYCLE]        = 0x32010000,
		[KB_MODE_DANCE]        = 0x80000000,
		[KB_MODE_FLASH]        = 0xA0000000,
		[KB_MODE_RANDOM_COLOR] = 0x70000000,
		[KB_MODE_TEMPO]        = 0x90000000,
		[KB_MODE_WAVE]         = 0xB0000000,
	};

	BUG_ON(mode >= ARRAY_SIZE(cmds));

	clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED, 0x20000000, NULL);

	if (mode == KB_MODE_CUSTOM) {
		kb_8_color__set_color(kb_backlight.color.left,
			kb_backlight.color.center,
			kb_backlight.color.right, kb_backlight.color.extra);
		kb_8_color__set_brightness(kb_backlight.brightness);
		return;
	}

	if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED,
		cmds[mode], NULL))
		kb_backlight.mode = mode;
}

static void kb_8_color__set_state(enum kb_state state)
{
	CLEVO_XSM_DEBUG("State: %d\n", state);

	switch (state) {
	case KB_STATE_OFF:
		if (!clevo_xsm_wmi_evaluate_wmbb_method(SET_KB_LED,
			0x22010000, NULL))
			kb_backlight.state = state;
		break;
	case KB_STATE_ON:
		kb_8_color__set_mode(kb_backlight.mode);
		kb_backlight.state = state;
		break;
	default:
		BUG();
	}
}

static void kb_8_color__init(void)
{
	CLEVO_XSM_DEBUG();

	kb_8_color__set_state(KB_STATE_OFF);

	kb_backlight.color.left   = param_kb_color[0];
	kb_backlight.color.center = param_kb_color[1];
	kb_backlight.color.right  = param_kb_color[2];

	kb_backlight.brightness = param_kb_brightness;
	kb_backlight.mode       = KB_MODE_CUSTOM;
	kb_backlight.extra      = KB_HAS_EXTRA_FALSE;

	if (!param_kb_off) {
		kb_8_color__set_color(kb_backlight.color.left,
			kb_backlight.color.center,
			kb_backlight.color.right, kb_backlight.color.extra);
		kb_8_color__set_brightness(kb_backlight.brightness);
		kb_8_color__set_state(KB_STATE_ON);
	}
}

static struct kb_backlight_ops kb_8_color_ops = {
	.set_state      = kb_8_color__set_state,
	.set_color      = kb_8_color__set_color,
	.set_brightness = kb_8_color__set_brightness,
	.set_mode       = kb_8_color__set_mode,
	.init           = kb_8_color__init,
};


#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
static void clevo_xsm_wmi_notify(union acpi_object *obj, void *context)
{
	/* On newer kernels, we don't get the u32 value directly. 
	 * We assume the event is relevant if we received it.
	 */
#else
static void clevo_xsm_wmi_notify(u32 value, void *context)
{
	if (value != 0xD0) {
		CLEVO_XSM_INFO("Unexpected WMI event (%0#6x)\n", value);
		return;
	}
#endif

	static unsigned int report_cnt;
	u32 event;

	clevo_xsm_wmi_evaluate_wmbb_method(GET_EVENT, 0, &event);
	
	/* Log all events to help identify key codes */
	CLEVO_XSM_INFO("WMI event received: 0x%02x\n", event);

	switch (event) {
	case 0xF4:
		CLEVO_XSM_DEBUG("Airplane-Mode Hotkey pressed\n");

		if (clevo_xsm_input_polling_task) {
			CLEVO_XSM_INFO("Stopping polling thread\n");
			kthread_stop(clevo_xsm_input_polling_task);
			clevo_xsm_input_polling_task = NULL;
		}

		mutex_lock(&clevo_xsm_input_report_mutex);

		if (global_report_cnt > report_cnt) {
			mutex_unlock(&clevo_xsm_input_report_mutex);
			break;
		}

		clevo_xsm_input_report_key(KEY_RFKILL);
		report_cnt++;

		mutex_unlock(&clevo_xsm_input_report_mutex);
		break;
	default:
		if (!kb_backlight.ops)
			break;

		switch (event) {
		case 0x81:
			kb_dec_brightness();
			break;
		case 0x82:
			kb_inc_brightness();
			break;
		case 0x83:
			if (!param_kb_cycle_colors)
				kb_next_mode();
			else
				kb_next_color();
			break;
		case 0x9F:
			kb_toggle_state();
			break;
		}
		break;
	}
}

static int clevo_xsm_wmi_probe(struct platform_device *dev)
{
	int status;

	status = wmi_install_notify_handler(CLEVO_EVENT_GUID,
		clevo_xsm_wmi_notify, NULL);
	if (unlikely(ACPI_FAILURE(status))) {
		CLEVO_XSM_ERROR("Could not register WMI notify handler (%0#6x)\n",
			status);
		return -EIO;
	}

	clevo_xsm_wmi_evaluate_wmbb_method(GET_AP, 0, NULL);

	if (kb_backlight.ops)
		kb_backlight.ops->init();

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void clevo_xsm_wmi_remove(struct platform_device *dev)
{
	wmi_remove_notify_handler(CLEVO_EVENT_GUID);
}
#else
static int clevo_xsm_wmi_remove(struct platform_device *dev)
{
	wmi_remove_notify_handler(CLEVO_EVENT_GUID);
	return 0;
}
#endif

static int clevo_xsm_wmi_resume(struct platform_device *dev)
{
	clevo_xsm_wmi_evaluate_wmbb_method(GET_AP, 0, NULL);

	if (kb_backlight.ops && kb_backlight.state == KB_STATE_ON)
		kb_backlight.ops->set_mode(kb_backlight.mode);

	return 0;
}

static struct platform_driver clevo_xsm_platform_driver = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	.remove = clevo_xsm_wmi_remove,
#else
	.remove = clevo_xsm_wmi_remove,
#endif
	.resume = clevo_xsm_wmi_resume,
	.driver = {
		.name  = CLEVO_XSM_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};


/* RFKILL sub-driver */

static bool param_rfkill;
module_param_named(rfkill, param_rfkill, bool, 0);
MODULE_PARM_DESC(rfkill, "Enable WWAN-RFKILL capability.");

static struct rfkill *clevo_xsm_wwan_rfkill_device;

static int clevo_xsm_wwan_rfkill_set_block(void *data, bool blocked)
{
	CLEVO_XSM_DEBUG("blocked=%i\n", blocked);

	if (clevo_xsm_wmi_evaluate_wmbb_method(SET_3G, !blocked, NULL))
		CLEVO_XSM_ERROR("Setting 3G power state failed!\n");
	return 0;
}

static const struct rfkill_ops clevo_xsm_wwan_rfkill_ops = {
	.set_block = clevo_xsm_wwan_rfkill_set_block,
};

static int __init clevo_xsm_rfkill_init(void)
{
	int err;
	u32 unblocked = 0;

	if (!param_rfkill)
		return 0;

	clevo_xsm_wmi_evaluate_wmbb_method(TALK_BIOS_3G, 1, NULL);

	clevo_xsm_wwan_rfkill_device = rfkill_alloc("clevo_xsm-wwan",
		&clevo_xsm_platform_device->dev,
		RFKILL_TYPE_WWAN,
		&clevo_xsm_wwan_rfkill_ops, NULL);
	if (unlikely(!clevo_xsm_wwan_rfkill_device))
		return -ENOMEM;

	err = rfkill_register(clevo_xsm_wwan_rfkill_device);
	if (unlikely(err))
		goto err_destroy_wwan;

	if (clevo_xsm_wmi_evaluate_wmbb_method(GET_POWER_STATE_FOR_3G, 0,
		&unblocked))
		CLEVO_XSM_ERROR("Could not get 3G power state!\n");
	else
		rfkill_set_sw_state(clevo_xsm_wwan_rfkill_device, !unblocked);

	return 0;

err_destroy_wwan:
	rfkill_destroy(clevo_xsm_wwan_rfkill_device);
	clevo_xsm_wmi_evaluate_wmbb_method(TALK_BIOS_3G, 0, NULL);
	return err;
}

static void __exit clevo_xsm_rfkill_exit(void)
{
	if (!clevo_xsm_wwan_rfkill_device)
		return;

	clevo_xsm_wmi_evaluate_wmbb_method(TALK_BIOS_3G, 0, NULL);

	rfkill_unregister(clevo_xsm_wwan_rfkill_device);
	rfkill_destroy(clevo_xsm_wwan_rfkill_device);
}


/* Sysfs interface */

static ssize_t clevo_xsm_brightness_show(struct device *child,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", kb_backlight.brightness);
}

static ssize_t clevo_xsm_brightness_store(struct device *child,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val;
	int ret;

	if (!kb_backlight.ops)
		return -EINVAL;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	kb_backlight.ops->set_brightness(val);

	return ret ? : size;
}

static DEVICE_ATTR(kb_brightness, 0644,
	clevo_xsm_brightness_show, clevo_xsm_brightness_store);

static ssize_t clevo_xsm_state_show(struct device *child,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", kb_backlight.state);
}

static ssize_t clevo_xsm_state_store(struct device *child,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val;
	int ret;

	if (!kb_backlight.ops)
		return -EINVAL;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	val = clamp_t(unsigned, val, 0, 1);
	kb_backlight.ops->set_state(val);

	return ret ? : size;
}

static DEVICE_ATTR(kb_state, 0644,
	clevo_xsm_state_show, clevo_xsm_state_store);

static ssize_t clevo_xsm_mode_show(struct device *child,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", kb_backlight.mode);
}

static ssize_t clevo_xsm_mode_store(struct device *child,
	struct device_attribute *attr, const char *buf, size_t size)
{
	static enum kb_mode modes[] = {
		KB_MODE_RANDOM_COLOR,
		KB_MODE_CUSTOM,
		KB_MODE_BREATHE,
		KB_MODE_CYCLE,
		KB_MODE_WAVE,
		KB_MODE_DANCE,
		KB_MODE_TEMPO,
		KB_MODE_FLASH,
	};

	unsigned int val;
	int ret;

	if (!kb_backlight.ops)
		return -EINVAL;

	ret = kstrtouint(buf, 0, &val);
	if (ret)
		return ret;

	val = clamp_t(unsigned, val, 0, 7);
	kb_backlight.ops->set_mode(modes[val]);

	return ret ? : size;
}

static DEVICE_ATTR(kb_mode, 0644,
	clevo_xsm_mode_show, clevo_xsm_mode_store);

static ssize_t clevo_xsm_color_show(struct device *child,
	struct device_attribute *attr, char *buf)
{
	if (kb_backlight.extra == KB_HAS_EXTRA_TRUE)
		return sprintf(buf, "%s %s %s %s\n",
			kb_colors[kb_backlight.color.left].name,
			kb_colors[kb_backlight.color.center].name,
			kb_colors[kb_backlight.color.right].name,
			kb_colors[kb_backlight.color.extra].name);
	else
		return sprintf(buf, "%s %s %s\n",
			kb_colors[kb_backlight.color.left].name,
			kb_colors[kb_backlight.color.center].name,
			kb_colors[kb_backlight.color.right].name);
}

static ssize_t clevo_xsm_color_store(struct device *child,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int i, j;
	unsigned int val[4] = {0};
	char left[8];
	char right[8];
	char center[8];
	char extra[8];

	if (!kb_backlight.ops)
		return -EINVAL;

	i = sscanf(buf, "%7s %7s %7s %7s", left, center, right, extra);

	if (i == 1) {
		for (j = 0; j < ARRAY_SIZE(kb_colors); j++) {
			if (!strcmp(left, kb_colors[j].name))
				val[0] = j;
		}
		val[0] = clamp_t(unsigned, val[0], 0, ARRAY_SIZE(kb_colors));
		val[3] = val[2] = val[1] = val[0];

	} else if (i == 3 || i == 4) {
		for (j = 0; j < ARRAY_SIZE(kb_colors); j++) {
			if (!strcmp(left, kb_colors[j].name))
				val[0] = j;
			if (!strcmp(center, kb_colors[j].name))
				val[1] = j;
			if (!strcmp(right, kb_colors[j].name))
				val[2] = j;
			if (!strcmp(extra, kb_colors[j].name))
				val[3] = j;
		}
		val[0] = clamp_t(unsigned, val[0], 0, ARRAY_SIZE(kb_colors));
		val[1] = clamp_t(unsigned, val[1], 0, ARRAY_SIZE(kb_colors));
		val[2] = clamp_t(unsigned, val[2], 0, ARRAY_SIZE(kb_colors));
		val[3] = clamp_t(unsigned, val[3], 0, ARRAY_SIZE(kb_colors));

	} else
		return -EINVAL;

	kb_backlight.ops->set_color(val[0], val[1], val[2], val[3]);

	return size;
}
static DEVICE_ATTR(kb_color, 0644,
	clevo_xsm_color_show, clevo_xsm_color_store);

/* Wave effect sysfs control */
static ssize_t clevo_xsm_wave_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", wave_running ? 1 : 0);
}

static ssize_t clevo_xsm_wave_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val;
	
	if (kstrtouint(buf, 10, &val))
		return -EINVAL;
	
	if (val)
		wave_start();
	else
		wave_stop();
	
	return size;
}
static DEVICE_ATTR(kb_wave, 0644,
	clevo_xsm_wave_show, clevo_xsm_wave_store);

static ssize_t clevo_xsm_wave_period_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", wave_interval_ms * NUM_WAVE_STEPS);
}

static ssize_t clevo_xsm_wave_period_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val;
	
	if (kstrtouint(buf, 10, &val))
		return -EINVAL;
	
	/* Minimum period: roughly 200ms (10ms interval) */
	if (val < 200) val = 200;
	
	wave_interval_ms = val / NUM_WAVE_STEPS;
	if (wave_interval_ms < 10) wave_interval_ms = 10;
	
	return size;
}
static DEVICE_ATTR(kb_wave_period, 0644,
	clevo_xsm_wave_period_show, clevo_xsm_wave_period_store);

static ssize_t clevo_xsm_wave_interval_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", wave_interval_ms);
}

static ssize_t clevo_xsm_wave_interval_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val;
	
	if (kstrtouint(buf, 10, &val))
		return -EINVAL;
	
	/* Minimum interval: 10ms */
	if (val < 10) val = 10;
	
	wave_interval_ms = val;
	
	return size;
}
static DEVICE_ATTR(kb_wave_interval, 0644,
	clevo_xsm_wave_interval_show, clevo_xsm_wave_interval_store);

/* LED Mode definitions - matching CC30 */
#define LED_MODE_STATIC  0
#define LED_MODE_WAVE    1
#define LED_MODE_BREATH  2
#define LED_MODE_BLINK   3

static int current_led_mode = LED_MODE_STATIC;
static struct delayed_work breath_work;
static struct delayed_work blink_work;
static bool breath_running = false;
static bool blink_running = false;
static unsigned int breath_step = 0;
static unsigned int blink_state = 0;

/* Breath effect - fade in/out without color change */
static void breath_work_handler(struct work_struct *work)
{
	static const u8 breath_levels[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 8, 7, 6, 5, 4, 3, 2, 1};
	#define NUM_BREATH_STEPS 18
	
	if (!breath_running)
		return;
	
	wave_set_brightness_direct(breath_levels[breath_step]);
	
	breath_step++;
	if (breath_step >= NUM_BREATH_STEPS)
		breath_step = 0;
	
	if (breath_running)
		queue_delayed_work(wave_workqueue, &breath_work, msecs_to_jiffies(100));
}

/* Blink effect - flash on/off */
static void blink_work_handler(struct work_struct *work)
{
	if (!blink_running)
		return;
	
	if (blink_state) {
		wave_set_brightness_direct(9);  /* Off (dim) */
		blink_state = 0;
	} else {
		wave_set_brightness_direct(0);  /* On (bright) */
		blink_state = 1;
	}
	
	if (blink_running)
		queue_delayed_work(wave_workqueue, &blink_work, msecs_to_jiffies(500));
}

static void stop_all_effects(void)
{
	wave_stop();
	breath_running = false;
	blink_running = false;
	if (wave_workqueue) {
		cancel_delayed_work_sync(&breath_work);
		cancel_delayed_work_sync(&blink_work);
	}
}

static void start_led_mode(int mode)
{
	stop_all_effects();
	
	if (!wave_workqueue)
		wave_workqueue = create_singlethread_workqueue("kb_wave_wq");
	
	current_led_mode = mode;
	
	switch (mode) {
	case LED_MODE_WAVE:
		wave_start();
		break;
	case LED_MODE_BREATH:
		breath_running = true;
		breath_step = 0;
		queue_delayed_work(wave_workqueue, &breath_work, 0);
		break;
	case LED_MODE_BLINK:
		blink_running = true;
		blink_state = 1;
		queue_delayed_work(wave_workqueue, &blink_work, 0);
		break;
	case LED_MODE_STATIC:
	default:
		wave_set_brightness_direct(0);  /* Max brightness */
		break;
	}
}

/* kb_led_mode sysfs - select LED effect mode */
static ssize_t clevo_xsm_led_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	const char *mode_names[] = {"static", "wave", "breath", "blink"};
	return sprintf(buf, "%d (%s)\n", current_led_mode, 
		mode_names[current_led_mode % 4]);
}

static ssize_t clevo_xsm_led_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val;
	
	/* Accept number or name */
	if (strncmp(buf, "static", 6) == 0)
		val = LED_MODE_STATIC;
	else if (strncmp(buf, "wave", 4) == 0)
		val = LED_MODE_WAVE;
	else if (strncmp(buf, "breath", 6) == 0)
		val = LED_MODE_BREATH;
	else if (strncmp(buf, "blink", 5) == 0)
		val = LED_MODE_BLINK;
	else if (kstrtouint(buf, 10, &val))
		return -EINVAL;
	
	if (val > LED_MODE_BLINK)
		return -EINVAL;
	
	start_led_mode(val);
	
	return size;
}
static DEVICE_ATTR(kb_led_mode, 0644,
	clevo_xsm_led_mode_show, clevo_xsm_led_mode_store);

/* Fan Control Mode: 0=auto, 1=max, 2=custom */
#define FAN_MODE_AUTO   0
#define FAN_MODE_MAX    1
#define FAN_MODE_CUSTOM 2

static int fan_control_mode = FAN_MODE_AUTO;

static void set_fan_mode(int mode)
{
	fan_control_mode = mode;
	
	switch (mode) {
	case FAN_MODE_MAX:
		/* Set fans to max speed - EC register 0xCE controls fan duty */
		/* Write 0xFF (100%) to force max speed */
		ec_write(0xCE, 0xFF);
		break;
	case FAN_MODE_AUTO:
	default:
		/* Restore auto control - write 0x00 to let EC manage */
		ec_write(0xCE, 0x00);
		break;
	}
}

static ssize_t clevo_xsm_fan_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	const char *mode_names[] = {"auto", "max", "custom"};
	return sprintf(buf, "%d (%s)\n", fan_control_mode,
		mode_names[fan_control_mode % 3]);
}

static ssize_t clevo_xsm_fan_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val;
	
	if (strncmp(buf, "auto", 4) == 0)
		val = FAN_MODE_AUTO;
	else if (strncmp(buf, "max", 3) == 0)
		val = FAN_MODE_MAX;
	else if (strncmp(buf, "custom", 6) == 0)
		val = FAN_MODE_CUSTOM;
	else if (kstrtouint(buf, 10, &val))
		return -EINVAL;
	
	if (val > FAN_MODE_CUSTOM)
		return -EINVAL;
	
	set_fan_mode(val);
	return size;
}
static DEVICE_ATTR(fan_control, 0644,
	clevo_xsm_fan_mode_show, clevo_xsm_fan_mode_store);

/* Power Profile: 0=performance, 1=entertainment, 2=power_saving, 3=quiet */
#define PROFILE_PERFORMANCE   0
#define PROFILE_ENTERTAINMENT 1
#define PROFILE_POWER_SAVING  2
#define PROFILE_QUIET         3

static int power_profile = PROFILE_POWER_SAVING;

static void set_power_profile(int profile)
{
	power_profile = profile;
	
	/* Power profile affects fan behavior and possibly CPU limits */
	/* Using WMI method 0x67 with profile-specific commands */
	switch (profile) {
	case PROFILE_PERFORMANCE:
		/* High performance - max fans, no throttling */
		clevo_xsm_wmi_evaluate_wmbb_method(0x67, 0xA3000000, NULL);
		set_fan_mode(FAN_MODE_MAX);
		break;
	case PROFILE_ENTERTAINMENT:
		/* Balanced - moderate fans */
		clevo_xsm_wmi_evaluate_wmbb_method(0x67, 0xA3000001, NULL);
		set_fan_mode(FAN_MODE_AUTO);
		break;
	case PROFILE_POWER_SAVING:
		/* Power saving - reduced performance */
		clevo_xsm_wmi_evaluate_wmbb_method(0x67, 0xA3000002, NULL);
		set_fan_mode(FAN_MODE_AUTO);
		break;
	case PROFILE_QUIET:
		/* Quiet - minimal fan noise */
		clevo_xsm_wmi_evaluate_wmbb_method(0x67, 0xA3000003, NULL);
		set_fan_mode(FAN_MODE_AUTO);
		break;
	}
}

static ssize_t clevo_xsm_power_profile_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	const char *profile_names[] = {"performance", "entertainment", 
		"power_saving", "quiet"};
	return sprintf(buf, "%d (%s)\n", power_profile,
		profile_names[power_profile % 4]);
}

static ssize_t clevo_xsm_power_profile_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int val;
	
	if (strncmp(buf, "performance", 11) == 0)
		val = PROFILE_PERFORMANCE;
	else if (strncmp(buf, "entertainment", 13) == 0)
		val = PROFILE_ENTERTAINMENT;
	else if (strncmp(buf, "power_saving", 12) == 0)
		val = PROFILE_POWER_SAVING;
	else if (strncmp(buf, "quiet", 5) == 0)
		val = PROFILE_QUIET;
	else if (kstrtouint(buf, 10, &val))
		return -EINVAL;
	
	if (val > PROFILE_QUIET)
		return -EINVAL;
	
	set_power_profile(val);
	return size;
}
static DEVICE_ATTR(power_profile, 0644,
	clevo_xsm_power_profile_show, clevo_xsm_power_profile_store);

#if CLEVO_HAS_HWMON
struct clevo_hwmon {
	struct device *dev;
};

static struct clevo_hwmon *clevo_hwmon = NULL;

static int
clevo_read_fan(int idx)
{
	u8 value;
	int raw_rpm;
	ec_read(0xd0 + 0x2 * idx, &value);
	raw_rpm = value << 8;
	ec_read(0xd1 + 0x2 * idx, &value);
	raw_rpm += value;
	if (!raw_rpm)
		return 0;
	return 2156220 / raw_rpm;
}

static ssize_t
clevo_hwmon_show_name(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return sprintf(buf, CLEVO_XSM_DRIVER_NAME "\n");
}

static ssize_t
clevo_hwmon_show_fan1_input(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%i\n", clevo_read_fan(0));
}

static ssize_t
clevo_hwmon_show_fan1_label(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "CPU fan\n");
}

#ifdef EXPERIMENTAL
static ssize_t
clevo_hwmon_show_fan2_input(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%i\n", clevo_read_fan(1));
}

static ssize_t
clevo_hwmon_show_fan2_label(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "GPU fan\n");
}
#endif

static ssize_t
clevo_hwmon_show_temp1_input(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	u8 value;
	ec_read(0x07, &value);
	return sprintf(buf, "%i\n", value * 1000);
}

static ssize_t
clevo_hwmon_show_temp1_label(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "CPU temperature\n");
}

#ifdef EXPERIMENTAL
static ssize_t
clevo_hwmon_show_temp2_input(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	u8 value;
	ec_read(0xcd, &value);
	return sprintf(buf, "%i\n", value * 1000);
}

static ssize_t
clevo_hwmon_show_temp2_label(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "GPU temperature\n");
}
#endif

static SENSOR_DEVICE_ATTR(name, S_IRUGO, clevo_hwmon_show_name, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, clevo_hwmon_show_fan1_input, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_label, S_IRUGO, clevo_hwmon_show_fan1_label, NULL, 0);
#ifdef EXPERIMENTAL
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, clevo_hwmon_show_fan2_input, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_label, S_IRUGO, clevo_hwmon_show_fan2_label, NULL, 0);
#endif
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, clevo_hwmon_show_temp1_input, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, clevo_hwmon_show_temp1_label, NULL, 0);
#ifdef EXPERIMENTAL
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, clevo_hwmon_show_temp2_input, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, clevo_hwmon_show_temp2_label, NULL, 0);
#endif


static struct attribute *hwmon_default_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_label.dev_attr.attr,
#ifdef EXPERIMENTAL
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_label.dev_attr.attr,
#endif
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_label.dev_attr.attr,
#ifdef EXPERIMENTAL
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_label.dev_attr.attr,
#endif
	NULL
};

static const struct attribute_group hwmon_default_attrgroup = {
	.attrs = hwmon_default_attributes,
};

static int
clevo_hwmon_init(struct device *dev)
{
	int ret;

	clevo_hwmon = kzalloc(sizeof(*clevo_hwmon), GFP_KERNEL);
	if (!clevo_hwmon)
		return -ENOMEM;
	clevo_hwmon->dev = hwmon_device_register(dev);
	if (IS_ERR(clevo_hwmon->dev)) {
		ret = PTR_ERR(clevo_hwmon->dev);
		clevo_hwmon->dev = NULL;
		return ret;
	}

	ret = sysfs_create_group(&clevo_hwmon->dev->kobj, &hwmon_default_attrgroup);
	if (ret)
		return ret;
	return 0;
}

static int
clevo_hwmon_fini(struct device *dev)
{
	if (!clevo_hwmon || !clevo_hwmon->dev)
		return 0;
	sysfs_remove_group(&clevo_hwmon->dev->kobj, &hwmon_default_attrgroup);
	hwmon_device_unregister(clevo_hwmon->dev);
	kfree(clevo_hwmon);
	return 0;
}
#endif // CLEVO_HAS_HWMON

/* dmi & init & exit */

static int __init clevo_xsm_dmi_matched(const struct dmi_system_id *id)
{
	CLEVO_XSM_INFO("Model %s found\n", id->ident);
	kb_backlight.ops = id->driver_data;

	return 1;
}

static struct dmi_system_id clevo_xsm_dmi_table[] __initdata = {
	{
		.ident = "Clevo P870DM",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P870DM"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "Clevo P7xxDM(-G)",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P7xxDM(-G)"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "Clevo P750ZM",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P750ZM"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "Clevo P370SM-A",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P370SM-A"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo P17SM-A",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P17SM-A"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "Clevo P15SM1-A",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P15SM1-A"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo P15SM-A",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P15SM-A"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo P17SM",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P17SM"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_8_color_ops,
	},
	{
		.ident = "Clevo P15SM",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P15SM"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_8_color_ops,
	},
	{
		.ident = "Clevo P150EM",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P150EM"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_8_color_ops,
	},
		{
		.ident = "Clevo P65_67RSRP",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P65_67RSRP"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo P65xRP",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P65xRP"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo P150EM",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P15xEMx"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_8_color_ops,
	},
	{
		.ident = "Clevo P7xxDM2(-G)",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P7xxDM2(-G)"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "Clevo P950HP6",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P95_HP,HR,HQ"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo N850HJ",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "N85_N87"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo P775DM3(-G)",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P775DM3(-G)"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo N850HJ",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "N85_N87"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		.ident = "Clevo N870HK",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "N85_N87,HJ,HJ1,HK1"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	/* Ones that don't follow the 'standard' product names above */
	{
		.ident = "Clevo P7xxDM(-G)",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Deimos/Phobos 1x15S"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "Clevo P750ZM",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P5 Pro SE"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "Clevo P750ZM",
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "P5 Pro"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "Clevo P750ZM",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ECT"),
			DMI_MATCH(DMI_BOARD_NAME, "P750ZM"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_with_extra_ops,
	},
	{
		.ident = "COLORFUL P15 23",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "COLORFUL"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P15 23"),
		},
		.callback = clevo_xsm_dmi_matched,
		.driver_data = &kb_full_color_ops,
	},
	{
		/* terminating NULL entry */
	},
};

MODULE_DEVICE_TABLE(dmi, clevo_xsm_dmi_table);

static int __init clevo_xsm_init(void)
{
	int err;

	switch (param_kb_color_num) {
	case 1:
		param_kb_color[1] = param_kb_color[2] = param_kb_color[0] = param_kb_color[3];
		break;
	case 2:
		return -EINVAL;
	}

	dmi_check_system(clevo_xsm_dmi_table);

	if (!wmi_has_guid(CLEVO_EVENT_GUID)) {
		CLEVO_XSM_INFO("No known WMI event notification GUID found\n");
		return -ENODEV;
	}

	if (!wmi_has_guid(CLEVO_GET_GUID)) {
		CLEVO_XSM_INFO("No known WMI control method GUID found\n");
		return -ENODEV;
	}

	clevo_xsm_platform_device =
		platform_create_bundle(&clevo_xsm_platform_driver,
			clevo_xsm_wmi_probe, NULL, 0, NULL, 0);

	if (unlikely(IS_ERR(clevo_xsm_platform_device)))
		return PTR_ERR(clevo_xsm_platform_device);

	err = clevo_xsm_rfkill_init();
	if (unlikely(err))
		CLEVO_XSM_ERROR("Could not register rfkill device\n");

	err = clevo_xsm_input_init();
	if (unlikely(err))
		CLEVO_XSM_ERROR("Could not register input device\n");

	err = clevo_xsm_led_init();
	if (unlikely(err))
		CLEVO_XSM_ERROR("Could not register LED device\n");

	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_brightness) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for brightness\n");

	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_state) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for state\n");

	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_mode) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for mode\n");

	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_color) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for color\n");

	/* Initialize wave effect */
	INIT_DELAYED_WORK(&wave_work, wave_work_handler);
	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_wave) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for wave\n");

	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_wave_period) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for wave period\n");

	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_wave_interval) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for wave interval\n");

	/* Initialize breath and blink effects */
	INIT_DELAYED_WORK(&breath_work, breath_work_handler);
	INIT_DELAYED_WORK(&blink_work, blink_work_handler);
	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_led_mode) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for kb_led_mode\n");
	
	/* Fan control and power profile */
	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_fan_control) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for fan_control\n");
	
	if (device_create_file(&clevo_xsm_platform_device->dev,
		&dev_attr_power_profile) != 0)
		CLEVO_XSM_ERROR("Sysfs attribute creation failed for power_profile\n");
#ifdef CLEVO_HAS_HWMON
	clevo_hwmon_init(&clevo_xsm_platform_device->dev);
#endif

	return 0;
}

static void __exit clevo_xsm_exit(void)
{
	clevo_xsm_led_exit();
	clevo_xsm_input_exit();
	clevo_xsm_rfkill_exit();

#ifdef CLEVO_HAS_HWMON
	clevo_hwmon_fini(&clevo_xsm_platform_device->dev);
#endif
	device_remove_file(&clevo_xsm_platform_device->dev,
		&dev_attr_kb_brightness);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_kb_state);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_kb_mode);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_kb_color);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_kb_wave);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_kb_wave_period);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_kb_wave_interval);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_kb_led_mode);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_fan_control);
	device_remove_file(&clevo_xsm_platform_device->dev, &dev_attr_power_profile);
	/* Stop all LED effects and cleanup workqueue */
	stop_all_effects();
	if (wave_workqueue)
		destroy_workqueue(wave_workqueue);

	platform_device_unregister(clevo_xsm_platform_device);
	platform_driver_unregister(&clevo_xsm_platform_driver);
}

module_init(clevo_xsm_init);
module_exit(clevo_xsm_exit);

MODULE_AUTHOR("TUXEDO Computer GmbH <tux@tuxedocomputers.com>");
MODULE_DESCRIPTION("Clevo SM series laptop driver.");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.1");
