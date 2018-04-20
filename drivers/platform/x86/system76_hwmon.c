/*
 * fan.c
 *
 * Copyright (C) 2017 Jeremy Soller <jeremy@system76.com>
 * Copyright (C) 2014-2016 Arnoud Willemsen <mail@lynthium.com>
 * Copyright (C) 2013-2015 TUXEDO Computers GmbH <tux@tuxedocomputers.com>
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

#define EXPERIMENTAL

#if S76_HAS_HWMON
struct s76_hwmon {
	struct device *dev;
};

static struct s76_hwmon *s76_hwmon = NULL;

static int
s76_read_fan(int idx) {
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

static int
s76_read_pwm(int idx) {
	u8 value;
	ec_read(0xce + idx, &value);
	return value;
}

static int
s76_write_pwm(int idx, u8 duty) {
	u8 values[] = {idx + 1, duty};
	return ec_transaction(0x99, values, sizeof(values), NULL, 0);
}

static int
s76_write_pwm_auto(int idx) {
	u8 values[] = {0xff, idx + 1};
	return ec_transaction(0x99, values, sizeof(values), NULL, 0);
}

static ssize_t
s76_hwmon_show_name(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	return sprintf(buf, S76_DRIVER_NAME "\n");
}

static ssize_t
s76_hwmon_show_fan_input(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "%i\n", s76_read_fan(index));
}

static ssize_t
s76_hwmon_show_fan_label(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	switch (to_sensor_dev_attr(attr)->index) {
	case 0:
		return sprintf(buf, "CPU fan\n");
	case 1:
		return sprintf(buf, "GPU fan\n");
	}
	return 0;
}

static int pwm_enabled[] = {2, 2};

static ssize_t
s76_hwmon_show_pwm(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "%i\n", s76_read_pwm(index));
}

static ssize_t
s76_hwmon_set_pwm(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 value;
	int err;
	int index = to_sensor_dev_attr(attr)->index;

	err = kstrtou32(buf, 10, &value);
	if (err) return err;
	if (value > 255) return -EINVAL;
	err = s76_write_pwm(index, value);
	if (err) return err;
	pwm_enabled[index] = 1;
	return count;
}

static ssize_t
s76_hwmon_show_pwm_enable(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "%i\n", pwm_enabled[index]);
}

static ssize_t
s76_hwmon_set_pwm_enable(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 value;
	int err;
	int index = to_sensor_dev_attr(attr)->index;

	err = kstrtou32(buf, 10, &value);
	if (err) return err;
	if (value == 0) {
		err = s76_write_pwm(index, 255);
		if (err) return err;
		pwm_enabled[index] = value;
		return count;
	}
	if (value == 1) {
		err = s76_write_pwm(index, 0);
		if (err) return err;
		pwm_enabled[index] = value;
		return count;
	}
	if (value == 2) {
		err = s76_write_pwm_auto(index);
		if (err) return err;
		pwm_enabled[index] = value;
		return count;
	}
	return -EINVAL;
}

static ssize_t
s76_hwmon_show_temp1_input(struct device *dev, struct device_attribute *attr, char *buf) {
	u8 value;
	ec_read(0x07, &value);
	return sprintf(buf, "%i\n", value * 1000);
}

static ssize_t
s76_hwmon_show_temp1_label(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf, "CPU temperature\n");
}

#ifdef EXPERIMENTAL
static ssize_t
s76_hwmon_show_temp2_input(struct device *dev, struct device_attribute *attr, char *buf) {
	u8 value;
	ec_read(0xcd, &value);
	return sprintf(buf, "%i\n", value * 1000);
}

static ssize_t
s76_hwmon_show_temp2_label(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf, "GPU temperature\n");
}
#endif

static SENSOR_DEVICE_ATTR(name, S_IRUGO, s76_hwmon_show_name, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, s76_hwmon_show_fan_input, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_label, S_IRUGO, s76_hwmon_show_fan_label, NULL, 0);
static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO |  S_IWUSR, s76_hwmon_show_pwm, s76_hwmon_set_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO |  S_IWUSR, s76_hwmon_show_pwm_enable, s76_hwmon_set_pwm_enable, 0);
#ifdef EXPERIMENTAL
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, s76_hwmon_show_fan_input, NULL, 1);
static SENSOR_DEVICE_ATTR(fan2_label, S_IRUGO, s76_hwmon_show_fan_label, NULL, 1);
static SENSOR_DEVICE_ATTR(pwm2, S_IRUGO |  S_IWUSR, s76_hwmon_show_pwm, s76_hwmon_set_pwm, 1);
static SENSOR_DEVICE_ATTR(pwm2_enable, S_IRUGO |  S_IWUSR, s76_hwmon_show_pwm_enable, s76_hwmon_set_pwm_enable, 1);
#endif
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, s76_hwmon_show_temp1_input, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, s76_hwmon_show_temp1_label, NULL, 0);
#ifdef EXPERIMENTAL
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, s76_hwmon_show_temp2_input, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, s76_hwmon_show_temp2_label, NULL, 1);
#endif

static struct attribute *hwmon_default_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_label.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
#ifdef EXPERIMENTAL
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_label.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
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

static int s76_hwmon_reboot_callback(struct notifier_block *nb,
		unsigned long action, void *data) {
	s76_write_pwm_auto(0);
	#ifdef EXPERIMENTAL
		s76_write_pwm_auto(1);
	#endif
	return NOTIFY_DONE;
}

static struct notifier_block s76_hwmon_reboot_notifier = {
	.notifier_call = s76_hwmon_reboot_callback
};

static int
s76_hwmon_init(struct device *dev) {
	int ret;

	s76_hwmon = kzalloc(sizeof(*s76_hwmon), GFP_KERNEL);
	if (!s76_hwmon)
		return -ENOMEM;
	s76_hwmon->dev = hwmon_device_register(dev);
	if (IS_ERR(s76_hwmon->dev)) {
		ret = PTR_ERR(s76_hwmon->dev);
		s76_hwmon->dev = NULL;
		return ret;
	}

	ret = sysfs_create_group(&s76_hwmon->dev->kobj, &hwmon_default_attrgroup);
	if (ret)
		return ret;

	register_reboot_notifier(&s76_hwmon_reboot_notifier);
	s76_write_pwm_auto(0);
	#ifdef EXPERIMENTAL
		s76_write_pwm_auto(1);
	#endif
	return 0;
}

static int
s76_hwmon_fini(struct device *dev) {
	if (!s76_hwmon || !s76_hwmon->dev)
		return 0;
	s76_write_pwm_auto(0);
	#ifdef EXPERIMENTAL
		s76_write_pwm_auto(1);
	#endif
	unregister_reboot_notifier(&s76_hwmon_reboot_notifier);
	sysfs_remove_group(&s76_hwmon->dev->kobj, &hwmon_default_attrgroup);
	hwmon_device_unregister(s76_hwmon->dev);
	kfree(s76_hwmon);
	return 0;
}
#endif // S76_HAS_HWMON
