#ifndef __HID_TMT500RS_H
#define __HID_TMT500RS_H

/* Standard Linux kernel includes */
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/fixp-arith.h>

/* Device identification */
#define USB_VENDOR_ID_THRUSTMASTER 0x044f

/* Protocol constants */
#define T500RS_MAX_EFFECTS         16
#define T500RS_BUFFER_LENGTH       64
#define T500RS_PACKET_HEADER_SIZE  23

/* Protocol Commands */
#define T500RS_CMD_START_EFFECT    0x41  /* Start effect (param 0x41 = start, 0x00 = stop) */
#define T500RS_CMD_STOP_EFFECT     0x41  /* Same as START but with different params */
#define T500RS_CMD_UPLOAD_EFFECT   0x01  /* Upload a new effect */
#define T500RS_CMD_MODIFY_EFFECT   0x02  /* Modify existing effect */
#define T500RS_CMD_SET_ENVELOPE    0x02  /* Set envelope parameters */
#define T500RS_CMD_SET_CONSTANT    0x03  /* Set constant force parameters */
#define T500RS_CMD_SET_PERIODIC    0x04  /* Set periodic effect parameters */
#define T500RS_CMD_SET_CONDITION   0x05  /* Set condition effect parameters */

/* Basic Effect Types */
#define T500RS_EFFECT_CONSTANT      0x00  /* Constant force */
#define T500RS_EFFECT_SPRING        0x40  /* Spring effect */
#define T500RS_EFFECT_FRICTION      0x41  /* Basic friction */
#define T500RS_EFFECT_DAMPER        0x41  /* Basic damper */
#define T500RS_EFFECT_INERTIA       0x41  /* Basic inertia */

/* Periodic Effect Types */
#define T500RS_EFFECT_SQUARE        0x20  /* Square wave */
#define T500RS_EFFECT_TRIANGLE      0x21  /* Triangle wave */
#define T500RS_EFFECT_SINE          0x22  /* Sine wave */
#define T500RS_EFFECT_SAWTOOTH_UP   0x23  /* Sawtooth up */
#define T500RS_EFFECT_SAWTOOTH_DOWN 0x24  /* Sawtooth down */
#define T500RS_EFFECT_RAMP          0x24  /* Ramp effect */

/* Extended Effect Types */
#define T500RS_EFFECT_AUTOCENTER    0x06  /* Auto-centering */
#define T500RS_EFFECT_INERTIA_2     0x07  /* Enhanced inertia */
#define T500RS_EFFECT_FRICTION_2    0x0c  /* Enhanced friction */
#define T500RS_EFFECT_DAMPER_2      0x0d  /* Enhanced damper */
#define T500RS_EFFECT_COMBINE       0x0f  /* Combined effects */

/* Effect Parameters Ranges */
#define T500RS_PARAM_LEVEL_MIN      0x00
#define T500RS_PARAM_LEVEL_MAX      0xff
#define T500RS_PARAM_COEF_MIN       0x00
#define T500RS_PARAM_COEF_MAX       0xff
#define T500RS_PARAM_DEADBAND_MIN   0x00
#define T500RS_PARAM_DEADBAND_MAX   0xff
#define T500RS_PARAM_CENTER_MIN     0x00
#define T500RS_PARAM_CENTER_MAX     0xff
#define T500RS_PARAM_PHASE_MIN      0x00
#define T500RS_PARAM_PHASE_MAX      0xff
#define T500RS_PARAM_PERIOD_MIN     0x00
#define T500RS_PARAM_PERIOD_MAX     0xff
#define T500RS_PARAM_MAGNITUDE_MIN  0x00
#define T500RS_PARAM_MAGNITUDE_MAX  0x7f
#define T500RS_PARAM_OFFSET_MIN     0x00
#define T500RS_PARAM_OFFSET_MAX     0xff
#define T500RS_PARAM_ATTACK_MIN     0x00
#define T500RS_PARAM_ATTACK_MAX     0xff
#define T500RS_PARAM_FADE_MIN       0x00
#define T500RS_PARAM_FADE_MAX       0xff

/* Effect State Flags */
#define T500RS_EFFECT_PLAYING      0x01
#define T500RS_EFFECT_MODIFIED     0x02
#define T500RS_EFFECT_UPLOADED     0x04
#define T500RS_EFFECT_STOPPED      0x08

/* Effect Parameters Structures */
struct t500rs_envelope {
    u16 attack_length;
    u8  attack_level;
    u16 fade_length;
    u8  fade_level;
};

struct t500rs_periodic {
    u8  waveform;
    u8  magnitude;
    u8  offset;
    u16 period;
    u8  phase;
};

struct t500rs_condition {
    u8 center;
    u8 deadband;
    u8 right_coeff;
    u8 left_coeff;
    u8 right_sat;
    u8 left_sat;
};

struct t500rs_effect_state {
    u8 id;
    u8 type;
    u8 status;
    u16 length;
    struct ff_effect effect;
    union {
        struct t500rs_envelope envelope;
        struct t500rs_periodic periodic;
        struct t500rs_condition condition;
    };
};

/* Device Data Structures */
struct t500rs_device_entry {
    struct hid_device *hdev;
    struct input_dev *input_dev;
    struct urb *urb;
    struct usb_device *usbdev;
    struct usb_interface *usbif;
    struct hid_report *report;
    struct hid_field *ff_field;
    struct t500rs_effect_state effects[T500RS_MAX_EFFECTS];
    u8 effects_used;
};

struct t500rs_data {
    unsigned long quirks;
    void *device_props;
};

/* Function Declarations */
int t500rs_init(struct hid_device *hdev);
void t500rs_remove(struct hid_device *hdev);
int t500rs_event(struct hid_device *hdev, struct hid_field *field,
                 struct hid_usage *usage, __s32 value);
int t500rs_upload_effect(struct input_dev *dev, struct ff_effect *effect,
                        struct ff_effect *old);
int t500rs_erase_effect(struct input_dev *dev, int effect_id);
int t500rs_play_effect(struct input_dev *dev, int effect_id, int value);
void t500rs_set_gain(struct input_dev *dev, u16 gain);
void t500rs_set_autocenter(struct input_dev *dev, u16 magnitude);

/* Report Descriptor */
const uint8_t t500_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x35, 0x00,        // Physical Minimum (0)
    0x45, 0x01,        // Physical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x0D,        // Report Count (13)
    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01,        // Usage Minimum (Button 1)
    0x29, 0x0D,        // Usage Maximum (Button 13)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x95, 0x03,        // Report Count (3)
    0x81, 0x01,        // Input (Constant)
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x25, 0x07,        // Logical Maximum (7)
    0x46, 0x3B, 0x01,  // Physical Maximum (315)
    0x75, 0x04,        // Report Size (4)
    0x95, 0x01,        // Report Count (1)
    0x65, 0x14,        // Unit (Degrees)
    0x09, 0x39,        // Usage (Hat switch)
    0x81, 0x42,        // Input (Data,Var,Abs,Null)
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x30,        // Usage (X)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x0F,  // Logical Maximum (4095)
    0x35, 0x00,        // Physical Minimum (0)
    0x46, 0xFF, 0x0F,  // Physical Maximum (4095)
    0x65, 0x00,        // Unit (None)
    0x75, 0x0C,        // Report Size (12)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x46, 0xFF, 0x00,  // Physical Maximum (255)
    0x09, 0x31,        // Usage (Y)
    0x09, 0x32,        // Usage (Z)
    0x09, 0x35,        // Usage (Rz)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x03,        // Report Count (3)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined)
    0x09, 0x20,        // Usage (0x20)
    0x09, 0x21,        // Usage (0x21)
    0x09, 0x22,        // Usage (0x22)
    0x09, 0x23,        // Usage (0x23)
    0x09, 0x24,        // Usage (0x24)
    0x09, 0x25,        // Usage (0x25)
    0x09, 0x26,        // Usage (0x26)
    0x09, 0x27,        // Usage (0x27)
    0x09, 0x28,        // Usage (0x28)
    0x09, 0x29,        // Usage (0x29)
    0x09, 0x2A,        // Usage (0x2A)
    0x09, 0x2B,        // Usage (0x2B)
    0x95, 0x0C,        // Report Count (12)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0x0A, 0x21, 0x26,  // Usage (Vendor defined 0x2621)
    0x95, 0x08,        // Report Count (8)
    0xB1, 0x02,        // Feature (Data,Var,Abs)
    0x0A, 0x21, 0x26,  // Usage (Vendor defined 0x2621)
    0x91, 0x02,        // Output (Data,Var,Abs)
    0x26, 0xFF, 0x03,  // Logical Maximum (1023)
    0x46, 0xFF, 0x03,  // Physical Maximum (1023)
    0x09, 0x2C,        // Usage (0x2C)
    0x09, 0x2D,        // Usage (0x2D)
    0x09, 0x2E,        // Usage (0x2E)
    0x09, 0x2F,        // Usage (0x2F)
    0x75, 0x10,        // Report Size (16)
    0x95, 0x04,        // Report Count (4)
    0x81, 0x02,        // Input (Data,Var,Abs)
    0xC0               // End Collection
};

/* Spring and Damper Values */
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

/* Firmware Request */
struct usb_ctrlrequest t500rs_firmware_request = {
    .bRequestType = 0xc1,
    .bRequest = 86,
    .wValue = 0,
    .wIndex = 0,
    .wLength = 8
};

/* Firmware Response */
struct __packed t500rs_firmware_response {
    uint8_t unknown0;
    uint8_t unknown1;
    uint8_t firmware_version;
    uint8_t unknown2;
};

#endif /* __HID_TMT500RS_H */
