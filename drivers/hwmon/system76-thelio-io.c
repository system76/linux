// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * system76-thelio-io.c - Linux driver for System76 Thelio Io
 * Copyright (C) 2023 System76
 *
 * Based on:
 * corsair-cpro.c - Linux driver for Corsair Commander Pro
 * Copyright (C) 2020 Marius Zachmann <mail@mariuszachmann.de>
 *
 * This driver uses hid reports to communicate with the device to allow hidraw userspace drivers
 * still being used. The device does not use report ids. When using hidraw and this driver
 * simultaniously, reports could be switched.
 */

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/types.h>

#define BUFFER_SIZE 32
#define REQ_TIMEOUT 300

#define HID_CMD 0
#define HID_RES 1
#define HID_DATA 2

#define CMD_FAN_GET  7
#define CMD_FAN_SET 8
#define CMD_LED_SET_MODE 16
#define CMD_FAN_TACH 22

struct thelio_io_device {
	struct hid_device *hdev;
	struct device *hwmon_dev;
#ifdef CONFIG_PM_SLEEP
	struct notifier_block pm_notifier;
#endif
	struct completion wait_input_report;
	struct mutex mutex; /* whenever buffer is used, lock before send_usb_cmd */
	u8 *buffer;
};

/* converts response error in buffer to errno */
static int thelio_io_get_errno(struct thelio_io_device *thelio_io)
{
	switch (thelio_io->buffer[HID_RES]) {
	case 0x00: /* success */
		return 0;
	default:
		hid_err(thelio_io->hdev, "unknown device response error: %d", thelio_io->buffer[HID_RES]);
		return -EIO;
	}
}

/* send command, check for error in response, response in thelio_io->buffer */
static int send_usb_cmd(struct thelio_io_device *thelio_io, u8 command, u8 byte1, u8 byte2, u8 byte3)
{
	unsigned long t;
	int ret;

	memset(thelio_io->buffer, 0x00, BUFFER_SIZE);
	thelio_io->buffer[HID_CMD] = command;
	thelio_io->buffer[HID_DATA] = byte1;
	thelio_io->buffer[HID_DATA + 1] = byte2;
	thelio_io->buffer[HID_DATA + 2] = byte3;

	reinit_completion(&thelio_io->wait_input_report);

	ret = hid_hw_output_report(thelio_io->hdev, thelio_io->buffer, BUFFER_SIZE);
	if (ret < 0)
		return ret;

	t = wait_for_completion_timeout(&thelio_io->wait_input_report, msecs_to_jiffies(REQ_TIMEOUT));
	if (!t)
		return -ETIMEDOUT;

	return thelio_io_get_errno(thelio_io);
}

static int thelio_io_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	struct thelio_io_device *thelio_io = hid_get_drvdata(hdev);

	/* only copy buffer when requested */
	if (completion_done(&thelio_io->wait_input_report))
		return 0;

	memcpy(thelio_io->buffer, data, min(BUFFER_SIZE, size));
	complete(&thelio_io->wait_input_report);

	return 0;
}

/* requests and returns single data values depending on channel */
static int get_data(struct thelio_io_device *thelio_io, int command, int channel, bool two_byte_data)
{
	int ret;

	mutex_lock(&thelio_io->mutex);

	ret = send_usb_cmd(thelio_io, command, channel, 0, 0);
	if (ret)
		goto out_unlock;

	ret = (int)thelio_io->buffer[HID_DATA + 1];
	if (two_byte_data)
		ret |= (((int)thelio_io->buffer[HID_DATA + 2]) << 8);

out_unlock:
	mutex_unlock(&thelio_io->mutex);
	return ret;
}

static int set_pwm(struct thelio_io_device *thelio_io, int channel, long val)
{
	int ret;

	if (val < 0 || val > 255)
		return -EINVAL;

	mutex_lock(&thelio_io->mutex);

	ret = send_usb_cmd(thelio_io, CMD_FAN_SET, channel, val, 0);

	mutex_unlock(&thelio_io->mutex);
	return ret;
}

static int thelio_io_read_string(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_label:
			switch (channel) {
				case 0:
					*str = "CPU Fan";
					return 0;
				case 1:
					*str = "Intake Fan";
					return 0;
				case 2:
					*str = "GPU Fan";
					return 0;
				case 3:
					*str = "Aux Fan";
					return 0;
				default:
					break;
			}
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int thelio_io_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct thelio_io_device *thelio_io = dev_get_drvdata(dev);
	int ret;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			ret = get_data(thelio_io, CMD_FAN_TACH, channel, true);
			if (ret < 0)
				return ret;
			*val = ret;
			return 0;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			ret = get_data(thelio_io, CMD_FAN_GET, channel, false);
			if (ret < 0)
				return ret;
			*val = ret;
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
};

static int thelio_io_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct thelio_io_device *thelio_io = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return set_pwm(thelio_io, channel, val);
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
};

static umode_t thelio_io_is_visible(const void *data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			return 0444;
		case hwmon_fan_label:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return 0644;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
};

static const struct hwmon_ops thelio_io_hwmon_ops = {
	.is_visible = thelio_io_is_visible,
	.read = thelio_io_read,
	.read_string = thelio_io_read_string,
	.write = thelio_io_write,
};

static const struct hwmon_channel_info *thelio_io_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL
			   ),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT
			   ),
	NULL
};

static const struct hwmon_chip_info thelio_io_chip_info = {
	.ops = &thelio_io_hwmon_ops,
	.info = thelio_io_info,
};

#ifdef CONFIG_PM_SLEEP
static int thelio_io_pm(struct notifier_block *nb, unsigned long action, void *data) {
	struct thelio_io_device * thelio_io = container_of(nb, struct thelio_io_device, pm_notifier);

	switch (action) {
		case PM_HIBERNATION_PREPARE:
		case PM_SUSPEND_PREPARE:
			mutex_lock(&thelio_io->mutex);
			send_usb_cmd(thelio_io, CMD_LED_SET_MODE, 0, 1, 0);
			mutex_unlock(&thelio_io->mutex);
			break;

		case PM_POST_HIBERNATION:
		case PM_POST_SUSPEND:
			mutex_lock(&thelio_io->mutex);
			send_usb_cmd(thelio_io, CMD_LED_SET_MODE, 0, 0, 0);
			mutex_unlock(&thelio_io->mutex);
			break;

		case PM_POST_RESTORE:
		case PM_RESTORE_PREPARE:
		default:
			break;
	}

	return NOTIFY_DONE;
}
#endif

static int thelio_io_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct thelio_io_device *thelio_io;
	int ret;

	thelio_io = devm_kzalloc(&hdev->dev, sizeof(*thelio_io), GFP_KERNEL);
	if (!thelio_io)
		return -ENOMEM;

	thelio_io->buffer = devm_kmalloc(&hdev->dev, BUFFER_SIZE, GFP_KERNEL);
	if (!thelio_io->buffer)
		return -ENOMEM;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto out_hw_stop;

	thelio_io->hdev = hdev;
	hid_set_drvdata(hdev, thelio_io);
	mutex_init(&thelio_io->mutex);
	init_completion(&thelio_io->wait_input_report);

	hid_device_io_start(hdev);

	if (hdev->maxcollection == 1 && hdev->collection[0].usage == 0xFF600061) {
		hid_info(hdev, "found command device\n");
		thelio_io->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "system76_thelio_io",
								 thelio_io, &thelio_io_chip_info, 0);
		if (IS_ERR(thelio_io->hwmon_dev)) {
			ret = PTR_ERR(thelio_io->hwmon_dev);
			goto out_hw_close;
		}

	#ifdef CONFIG_PM_SLEEP
		thelio_io->pm_notifier.notifier_call = thelio_io_pm;
		register_pm_notifier(&thelio_io->pm_notifier);
	#endif
	}

	return 0;

out_hw_close:
	hid_hw_close(hdev);
out_hw_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void thelio_io_remove(struct hid_device *hdev)
{
	struct thelio_io_device *thelio_io = hid_get_drvdata(hdev);

#ifdef CONFIG_PM_SLEEP
	unregister_pm_notifier(&thelio_io->pm_notifier);
#endif

	if (thelio_io->hwmon_dev) {
		hwmon_device_unregister(thelio_io->hwmon_dev);
	}
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id thelio_io_devices[] = {
	{ HID_USB_DEVICE(0x3384, 0x000B) }, // thelio_io_2
	{ }
};

static struct hid_driver thelio_io_driver = {
	.name = "system76-thelio-io",
	.id_table = thelio_io_devices,
	.probe = thelio_io_probe,
	.remove = thelio_io_remove,
	.raw_event = thelio_io_raw_event,
};

MODULE_DEVICE_TABLE(hid, thelio_io_devices);
MODULE_LICENSE("GPL");

static int __init thelio_io_init(void)
{
	return hid_register_driver(&thelio_io_driver);
}

static void __exit thelio_io_exit(void)
{
	hid_unregister_driver(&thelio_io_driver);
}

/*
 * When compiling this driver as built-in, hwmon initcalls will get called before the
 * hid driver and this driver would fail to register. late_initcall solves this.
 */
late_initcall(thelio_io_init);
module_exit(thelio_io_exit);
