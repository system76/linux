/*
 * input.c
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

#define AIRPLANE_KEY KEY_WLAN
#define TOUCHPAD_ON_KEY KEY_F21
#define TOUCHPAD_OFF_KEY KEY_F21

static struct input_dev *s76_input_device;
static DEFINE_MUTEX(s76_input_report_mutex);

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

static struct task_struct *s76_input_polling_task;

static void s76_input_key(unsigned int code) {
	S76_INFO("Send key %x\n", code);

	mutex_lock(&s76_input_report_mutex);

	input_report_key(s76_input_device, code, 1);
	input_sync(s76_input_device);

	input_report_key(s76_input_device, code, 0);
	input_sync(s76_input_device);

	mutex_unlock(&s76_input_report_mutex);
}

static int s76_input_polling_thread(void *data) {
	S76_INFO("Polling thread started (PID: %i), polling at %i Hz\n",
				current->pid, param_poll_freq);

	while (!kthread_should_stop()) {
		u8 byte;

		ec_read(0xDB, &byte);
		if (byte & 0x40) {
			ec_write(0xDB, byte & ~0x40);

			S76_INFO("Airplane-Mode Hotkey pressed (EC)\n");

			s76_input_key(AIRPLANE_KEY);
		}

		msleep_interruptible(1000 / param_poll_freq);
	}

	S76_INFO("Polling thread exiting\n");

	return 0;
}

static void s76_input_airplane_wmi(void) {
	S76_INFO("Airplane-Mode Hotkey pressed (WMI)\n");

	if (s76_input_polling_task) {
		S76_INFO("Stopping polling thread\n");
		kthread_stop(s76_input_polling_task);
		s76_input_polling_task = NULL;
	}

	s76_input_key(AIRPLANE_KEY);
}

static void s76_input_touchpad_wmi(bool enabled) {
	S76_INFO("Touchpad Hotkey pressed (WMI) %d\n", enabled);

	if (enabled) {
		s76_input_key(TOUCHPAD_ON_KEY);
	} else {
		s76_input_key(TOUCHPAD_OFF_KEY);
	}
}

static int s76_input_open(struct input_dev *dev) {
	s76_input_polling_task = kthread_run(
		s76_input_polling_thread,
		NULL, "system76-polld");

	if (unlikely(IS_ERR(s76_input_polling_task))) {
		s76_input_polling_task = NULL;
		S76_ERROR("Could not create polling thread\n");
		return PTR_ERR(s76_input_polling_task);
	}

	return 0;
}

static void s76_input_close(struct input_dev *dev) {
	if (unlikely(IS_ERR_OR_NULL(s76_input_polling_task))) {
		return;
	}

	kthread_stop(s76_input_polling_task);
	s76_input_polling_task = NULL;
}

static int __init s76_input_init(struct device *dev) {
	int err;
	u8 byte;

	s76_input_device = input_allocate_device();
	if (unlikely(!s76_input_device)) {
		S76_ERROR("Error allocating input device\n");
		return -ENOMEM;
	}

	s76_input_device->name = "System76 Airplane-Mode Hotkey";
	s76_input_device->phys = "system76/input0";
	s76_input_device->id.bustype = BUS_HOST;
	s76_input_device->dev.parent = dev;
	set_bit(EV_KEY, s76_input_device->evbit);
	set_bit(AIRPLANE_KEY, s76_input_device->keybit);
	set_bit(TOUCHPAD_ON_KEY, s76_input_device->keybit);
	set_bit(TOUCHPAD_OFF_KEY, s76_input_device->keybit);

	s76_input_device->open  = s76_input_open;
	s76_input_device->close = s76_input_close;

	ec_read(0xDB, &byte);
	ec_write(0xDB, byte & ~0x40);

	err = input_register_device(s76_input_device);
	if (unlikely(err)) {
		S76_ERROR("Error registering input device\n");
		goto err_free_input_device;
	}

	return 0;

err_free_input_device:
	input_free_device(s76_input_device);

	return err;
}

static void __exit s76_input_exit(void) {
	if (unlikely(!s76_input_device)) {
		return;
	}

	input_unregister_device(s76_input_device);
	s76_input_device = NULL;
}
