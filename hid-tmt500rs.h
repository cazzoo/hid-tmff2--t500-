/* SPDX-License-Identifier: GLP-2.0 */
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/fixp-arith.h>

//#include "hid-ids.h"

#define USB_VENDOR_ID_THRUSTMASTER 0x044f

#define T500RS_MAX_EFFECTS 16
#define T500RS_BUFFER_LENGTH 63

/* the wheel seems to only be capable of processing a certain number of
 * interrupts per second, and if this value is too low the kernel urb buffer(or
 * some buffer at least) fills up. Optimally I would figure out some way to
 * space out the interrupts so that they all leave at regular intervals, but
 * for now this is good enough, go slow enough that everything works.
 */
#define DEFAULT_TIMER_PERIOD 8

#define FF_EFFECT_QUEUE_UPLOAD 0
#define FF_EFFECT_QUEUE_START 1
#define FF_EFFECT_QUEUE_STOP 2
#define FF_EFFECT_PLAYING 3
#define FF_EFFECT_QUEUE_UPDATE 4

#define CLAMP_VALUE_U16(x) ((unsigned short)((x) > 0xffff ? 0xffff : (x)))
#define SCALE_VALUE_U16(x, bits) (CLAMP_VALUE_U16(x) >> (16 - bits))
#define JIFFIES2MS(jiffies) ((jiffies) * 1000 / HZ)

spinlock_t lock;
unsigned long lock_flags;

spinlock_t data_lock;
unsigned long data_flags;

static const signed short t500rs_ff_effects[] = {
		FF_CONSTANT,
		FF_RAMP,
		FF_SPRING,
		FF_DAMPER,
		FF_FRICTION,
		FF_INERTIA,
		FF_PERIODIC,
		FF_SINE,
		FF_TRIANGLE,
		FF_SQUARE,
		FF_SAW_UP,
		FF_SAW_DOWN,
		FF_AUTOCENTER,
		FF_GAIN,
		-1
};

struct t500rs_effect_state {
		struct ff_effect effect;
		struct ff_effect old;
		bool old_set;
		unsigned long flags;
		unsigned long start_time;
		unsigned long count;
};

struct __packed t500rs_firmware_response {
		uint8_t unknown0;
		uint8_t unknown1;
		uint8_t firmware_version;
		uint8_t	unknown2;
};


struct usb_ctrlrequest t500rs_firmware_request = {
		.bRequestType = 0xc1,
		.bRequest = 86,
		.wValue = 0,
		.wIndex = 0,
		.wLength = 8
};


struct t500rs_device_entry {
		struct hid_device *hdev;
		struct input_dev *input_dev;
		struct hid_report *report;
		struct hid_field *ff_field;
		struct usb_device *usbdev;
		struct usb_interface *usbif;
		struct t500rs_effect_state *states;
		struct t500rs_firmware_response *firmware_response;
		struct hrtimer hrtimer;

		int (*open)(struct input_dev *dev);
		void (*close)(struct input_dev *dev);

		spinlock_t lock;
		unsigned long lock_flags;

		u8 *send_buffer;

		u16 range;
		u8 effects_used;
};


struct t500rs_data {
		unsigned long quirks;
		void *device_props;
};

const uint8_t t500_report_descriptor[] = {
		0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
		0x09, 0x04,        // Usage (Joystick)
		0xA1, 0x01,        // Collection (Application)
		0x85, 0x01,        //   Report ID (1)
		0x09, 0x01,        //   Usage (Pointer)
		0xA1, 0x00,        //   Collection (Physical)
		0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
		0x09, 0x30,        //     Usage (X)
		0x09, 0x31,        //     Usage (Y)
		0x09, 0x32,        //     Usage (Z)
		0x09, 0x35,        //     Usage (Rz)
		0x15, 0x00,        //     Logical Minimum (0)
		0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
		0x75, 0x10,        //     Report Size (16)
		0x95, 0x04,        //     Report Count (4)
		0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
		0x05, 0x09,        //     Usage Page (Button)
		0x19, 0x01,        //     Usage Minimum (1)
		0x29, 0x10,        //     Usage Maximum (16)
		0x15, 0x00,        //     Logical Minimum (0)
		0x25, 0x01,        //     Logical Maximum (1)
		0x75, 0x01,        //     Report Size (1)
		0x95, 0x10,        //     Report Count (16)
		0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
		0xC0,              //   End Collection
};

static u8 spring_values[] = {
		0xa6, 0x6a, 0xa6, 0x6a, 0xfe,
		0xff, 0xfe, 0xff, 0xfe, 0xff,
		0xfe, 0xff, 0xdf, 0x58, 0xa6,
		0x6a, 0x06
};

static u8 damper_values[] = {
		0xfc, 0x7f, 0xfc, 0x7f, 0xfe,
		0xff, 0xfe, 0xff, 0xfe, 0xff,
		0xfe, 0xff, 0xfc, 0x7f, 0xfc,
		0x7f, 0x07
};
