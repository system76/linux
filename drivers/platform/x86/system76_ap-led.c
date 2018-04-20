/*
 * ap_led.c
 *
 * Copyright (C) 2017 Jeremy Soller <jeremy@system76.com>
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

static enum led_brightness ap_led_brightness = 1;

static bool ap_led_invert = TRUE;

static enum led_brightness ap_led_get(struct led_classdev *led_cdev) {
	return ap_led_brightness;
}

static int ap_led_set(struct led_classdev *led_cdev, enum led_brightness value) {
	u8 byte;
	
	ec_read(0xD9, &byte);
	
	if (value > 0) {
		ap_led_brightness = 1;
		
		if (ap_led_invert) {
			byte &= ~0x40;
		} else {
			byte |= 0x40;
		}
	} else {
		ap_led_brightness = 0;
		
		if (ap_led_invert) {
			byte |= 0x40;
		} else {
			byte &= ~0x40;
		}
	}
	
	ec_write(0xD9, byte);
	
	return 0;
}

static struct led_classdev ap_led = {
   .name = "system76::airplane",
   .brightness_get = ap_led_get,
   .brightness_set_blocking = ap_led_set,
   .max_brightness = 1,
   .default_trigger = "rfkill-any"
};

static ssize_t ap_led_invert_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf, "%d\n", (int)ap_led_invert);
}

static ssize_t ap_led_invert_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size) {
	unsigned int val;
	int ret;
	enum led_brightness brightness;

	ret = kstrtouint(buf, 0, &val);
	if (ret) {
		return ret;
	}
	
	brightness = ap_led_get(&ap_led);
	
	if (val) {
		ap_led_invert = TRUE;
	} else {
		ap_led_invert = FALSE;
	}
	
	ap_led_set(&ap_led, brightness);
	
	return size;
}

static struct device_attribute ap_led_invert_dev_attr = {
	.attr = {
		.name = "invert",
		.mode = 0644,
	},
	.show = ap_led_invert_show,
	.store = ap_led_invert_store,
};

static void ap_led_resume(void) {
	ap_led_set(&ap_led, ap_led_brightness);
}

static int __init ap_led_init(struct device *dev) {
	int err;

	err = led_classdev_register(dev, &ap_led);
	if (unlikely(err)) {
		return err;
	}
	
	if (device_create_file(ap_led.dev, &ap_led_invert_dev_attr) != 0) {
		S76_ERROR("failed to create ap_led_invert\n");
	}
	
	ap_led_resume();

	return 0;
}

static void __exit ap_led_exit(void) {
	device_remove_file(ap_led.dev, &ap_led_invert_dev_attr);
	
	if (!IS_ERR_OR_NULL(ap_led.dev)) {
		led_classdev_unregister(&ap_led);
	}
}
