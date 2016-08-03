// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/common/hid.h>
#include <ddk/protocol/input.h>
#include <hw/inout.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls-ddk.h>
#include <magenta/types.h>

#include <hid/usages.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <runtime/mutex.h>
#include <runtime/thread.h>

#include <mxio/debug.h>
#define MXDEBUG 0

typedef struct i8042_keyboard_device {
    mx_device_t device;

    mx_handle_t irq;
    mxr_thread_t* irq_thread;

    int last_code;

    boot_kbd_report_t report;

    mx_hid_fifo_t fifo;
} i8042_device_t;

static inline bool is_modifier(uint8_t usage) {
    return (usage >= HID_USAGE_KEY_LEFT_CTRL && usage <= HID_USAGE_KEY_RIGHT_GUI);
}

#define MOD_SET 1
#define MOD_EXISTS 2
#define MOD_ROLLOVER 3
static int i8042_modifier(i8042_device_t* dev, uint8_t mod, bool down) {
    int bit = mod - HID_USAGE_KEY_LEFT_CTRL;
    if (bit < 0 || bit > 7) return MOD_ROLLOVER;
    if (down) {
        if (dev->report.modifier & 1 << bit) {
            return MOD_EXISTS;
        } else {
            dev->report.modifier |= 1 << bit;
        }
    } else {
        dev->report.modifier &= ~(1 << bit);
    }
    return MOD_SET;
}

#define KEY_ADDED 1
#define KEY_EXISTS 2
#define KEY_ROLLOVER 3
static int i8042_add_key(i8042_device_t* dev, uint8_t usage) {
    for (int i = 0; i < 6; i++) {
        if (dev->report.usage[i] == usage) return KEY_EXISTS;
        if (dev->report.usage[i] == 0) {
            dev->report.usage[i] = usage;
            return KEY_ADDED;
        }
    }
    return KEY_ROLLOVER;
}

#define KEY_REMOVED 1
#define KEY_NOT_FOUND 2
static int i8042_rm_key(i8042_device_t* dev, uint8_t usage) {
    int idx = -1;
    for (int i = 0; i < 6; i++) {
        if (dev->report.usage[i] == usage) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return KEY_NOT_FOUND;
    for (int i = idx; i < 5; i++) {
        dev->report.usage[i] = dev->report.usage[i+1];
    }
    dev->report.usage[5] = 0;
    return KEY_REMOVED;
}

#define get_kbd_device(dev) containerof(dev, i8042_device_t, device)

#define I8042_COMMAND_REG 0x64
#define I8042_STATUS_REG 0x64
#define I8042_DATA_REG 0x60

#define ISA_IRQ_KEYBOARD 0x1

static inline int i8042_read_data(void) {
    return inp(I8042_DATA_REG);
}

static inline int i8042_read_status(void) {
    return inp(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val) {
    outp(I8042_DATA_REG, val);
}

static inline void i8042_write_command(int val) {
    outp(I8042_COMMAND_REG, val);
}

/*
 * timeout in milliseconds
 */
#define I8042_CTL_TIMEOUT 500

/*
 * status register bits
 */
#define I8042_STR_PARITY 0x80
#define I8042_STR_TIMEOUT 0x40
#define I8042_STR_AUXDATA 0x20
#define I8042_STR_KEYLOCK 0x10
#define I8042_STR_CMDDAT 0x08
#define I8042_STR_MUXERR 0x04
#define I8042_STR_IBF 0x02
#define I8042_STR_OBF 0x01

/*
 * control register bits
 */
#define I8042_CTR_KBDINT 0x01
#define I8042_CTR_AUXINT 0x02
#define I8042_CTR_IGNKEYLK 0x08
#define I8042_CTR_KBDDIS 0x10
#define I8042_CTR_AUXDIS 0x20
#define I8042_CTR_XLATE 0x40

/*
 * commands
 */
#define I8042_CMD_CTL_RCTR 0x0120
#define I8042_CMD_CTL_WCTR 0x1060
#define I8042_CMD_CTL_TEST 0x01aa

#define I8042_CMD_KBD_DIS 0x00ad
#define I8042_CMD_KBD_EN 0x00ae
#define I8042_CMD_PULSE_RESET 0x00fe
#define I8042_CMD_KBD_TEST 0x01ab
#define I8042_CMD_KBD_MODE 0x01f0

/*
 * used for flushing buffers. the i8042 internal buffer shoudn't exceed this.
 */
#define I8042_BUFFER_LENGTH 32

static const uint8_t hid_report_desc[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x07,  //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,  //   Usage Minimum (0xE0)
    0x29, 0xE7,  //   Usage Maximum (0xE7)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x05, 0x08,  //   Usage Page (LEDs)
    0x19, 0x01,  //   Usage Minimum (Num Lock)
    0x29, 0x05,  //   Usage Maximum (Kana)
    0x91, 0x02,  //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,  //   Usage Minimum (0x00)
    0x29, 0x65,  //   Usage Maximum (0x65)
    0x81, 0x00,  //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,        // End Collection
};

static const uint8_t pc_set1_usage_map[128] = {
    /* 0x00 */ 0, HID_USAGE_KEY_ESC, HID_USAGE_KEY_1, HID_USAGE_KEY_2,
    /* 0x04 */ HID_USAGE_KEY_3, HID_USAGE_KEY_4, HID_USAGE_KEY_5, HID_USAGE_KEY_6,
    /* 0x08 */ HID_USAGE_KEY_7, HID_USAGE_KEY_8, HID_USAGE_KEY_9, HID_USAGE_KEY_0,
    /* 0x0c */ HID_USAGE_KEY_MINUS, HID_USAGE_KEY_EQUAL, HID_USAGE_KEY_BACKSPACE, HID_USAGE_KEY_TAB,
    /* 0x10 */ HID_USAGE_KEY_Q, HID_USAGE_KEY_W, HID_USAGE_KEY_E, HID_USAGE_KEY_R,
    /* 0x14 */ HID_USAGE_KEY_T, HID_USAGE_KEY_Y, HID_USAGE_KEY_U, HID_USAGE_KEY_I,
    /* 0x18 */ HID_USAGE_KEY_O, HID_USAGE_KEY_P, HID_USAGE_KEY_LEFTBRACE, HID_USAGE_KEY_RIGHTBRACE,
    /* 0x1c */ HID_USAGE_KEY_ENTER, HID_USAGE_KEY_LEFT_CTRL, HID_USAGE_KEY_A, HID_USAGE_KEY_S,
    /* 0x20 */ HID_USAGE_KEY_D, HID_USAGE_KEY_F, HID_USAGE_KEY_G, HID_USAGE_KEY_H,
    /* 0x24 */ HID_USAGE_KEY_J, HID_USAGE_KEY_K, HID_USAGE_KEY_L, HID_USAGE_KEY_SEMICOLON,
    /* 0x28 */ HID_USAGE_KEY_APOSTROPHE, HID_USAGE_KEY_GRAVE, HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_BACKSLASH,
    /* 0x2c */ HID_USAGE_KEY_Z, HID_USAGE_KEY_X, HID_USAGE_KEY_C, HID_USAGE_KEY_V,
    /* 0x30 */ HID_USAGE_KEY_B, HID_USAGE_KEY_N, HID_USAGE_KEY_M, HID_USAGE_KEY_COMMA,
    /* 0x34 */ HID_USAGE_KEY_DOT, HID_USAGE_KEY_SLASH, HID_USAGE_KEY_RIGHT_SHIFT, HID_USAGE_KEY_KP_ASTERISK,
    /* 0x38 */ HID_USAGE_KEY_LEFT_ALT, HID_USAGE_KEY_SPACE, HID_USAGE_KEY_CAPSLOCK, HID_USAGE_KEY_F1,
    /* 0x3c */ HID_USAGE_KEY_F2, HID_USAGE_KEY_F3, HID_USAGE_KEY_F4, HID_USAGE_KEY_F5,
    /* 0x40 */ HID_USAGE_KEY_F6, HID_USAGE_KEY_F7, HID_USAGE_KEY_F8, HID_USAGE_KEY_F9,
    /* 0x44 */ HID_USAGE_KEY_F10, HID_USAGE_KEY_NUMLOCK, HID_USAGE_KEY_SCROLLLOCK, HID_USAGE_KEY_KP_7,
    /* 0x48 */ HID_USAGE_KEY_KP_8, HID_USAGE_KEY_KP_9, HID_USAGE_KEY_KP_MINUS, HID_USAGE_KEY_KP_4,
    /* 0x4c */ HID_USAGE_KEY_KP_5, HID_USAGE_KEY_KP_6, HID_USAGE_KEY_KP_PLUS, HID_USAGE_KEY_KP_1,
    /* 0x50 */ HID_USAGE_KEY_KP_2, HID_USAGE_KEY_KP_3, HID_USAGE_KEY_KP_0, HID_USAGE_KEY_KP_DOT,
    /* 0x54 */ 0, 0, 0, HID_USAGE_KEY_F11,
    /* 0x58 */ HID_USAGE_KEY_F12, 0, 0, 0,
};

static const uint8_t pc_set1_usage_map_e0[128] = {
    /* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x08 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x10 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x18 */ 0, 0, 0, 0, HID_USAGE_KEY_KP_ENTER, HID_USAGE_KEY_RIGHT_CTRL, 0, 0,
    /* 0x20 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x28 */ 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x30 */ 0, 0, 0, 0, 0, HID_USAGE_KEY_KP_SLASH, 0, HID_USAGE_KEY_PRINTSCREEN,
    /* 0x38 */ HID_USAGE_KEY_RIGHT_ALT, 0, 0, 0, 0, 0, 0, 0,
    /* 0x40 */ 0, 0, 0, 0, 0, 0, 0, HID_USAGE_KEY_HOME,
    /* 0x48 */ HID_USAGE_KEY_UP, HID_USAGE_KEY_PAGEUP, 0, HID_USAGE_KEY_LEFT, 0, HID_USAGE_KEY_RIGHT, 0, HID_USAGE_KEY_END,
    /* 0x50 */ HID_USAGE_KEY_DOWN, HID_USAGE_KEY_PAGEDOWN, HID_USAGE_KEY_INSERT, 0, 0, 0, 0, 0,
    /* 0x58 */ 0, 0, 0, HID_USAGE_KEY_LEFT_GUI, HID_USAGE_KEY_RIGHT_GUI, 0 /* MENU */, 0, 0,
};

static int i8042_wait_read(void) {
    int i = 0;
    while ((~i8042_read_status() & I8042_STR_OBF) && (i < I8042_CTL_TIMEOUT)) {
        usleep(10);
        i++;
    }
    return -(i == I8042_CTL_TIMEOUT);
}

static int i8042_wait_write(void) {
    int i = 0;
    while ((i8042_read_status() & I8042_STR_IBF) && (i < I8042_CTL_TIMEOUT)) {
        usleep(10);
        i++;
    }
    return -(i == I8042_CTL_TIMEOUT);
}

static int i8042_flush(void) {
    unsigned char data __UNUSED;
    int i = 0;

    while ((i8042_read_status() & I8042_STR_OBF) && (i++ < I8042_BUFFER_LENGTH)) {
        usleep(10);
        data = i8042_read_data();
    }

    return i;
}

static int i8042_command(uint8_t* param, int command) {
    int retval = 0, i = 0;

    retval = i8042_wait_write();
    if (!retval) {
        i8042_write_command(command & 0xff);
    }

    if (!retval) {
        for (i = 0; i < ((command >> 12) & 0xf); i++) {
            if ((retval = i8042_wait_write())) {
                break;
            }

            i8042_write_data(param[i]);
        }
    }

    if (!retval) {
        for (i = 0; i < ((command >> 8) & 0xf); i++) {
            if ((retval = i8042_wait_read())) {
                break;
            }

            if (i8042_read_status() & I8042_STR_AUXDATA) {
                param[i] = ~i8042_read_data();
            } else {
                param[i] = i8042_read_data();
            }
        }
    }

    return retval;
}

static int i8042_selftest(void) {
    uint8_t param;
    int i = 0;
    do {
        if (i8042_command(&param, I8042_CMD_CTL_TEST)) {
            return -1;
        }
        if (param == 0x55)
            return 0;
        usleep(50 * 1000);
    } while (i++ < 5);
    return 0;
}

static int keyboard_command(uint8_t* param, int command) {
    int retval = 0, i = 0;

    retval = i8042_wait_write();
    if (!retval) {
        i8042_write_data(command & 0xff);
    }

    if (!retval) {
        for (i = 0; i < ((command >> 12) & 0xf); i++) {
            if ((retval = i8042_wait_write())) {
                break;
            }

            i8042_write_data(param[i]);
        }
    }

    if (!retval) {
        for (i = 0; i < ((command >> 8) & 0xf); i++) {
            if ((retval = i8042_wait_read())) {
                break;
            }

            if (i8042_read_status() & I8042_STR_AUXDATA) {
                param[i] = ~i8042_read_data();
            } else {
                param[i] = i8042_read_data();
            }
        }
    }

    return retval;
}

static void i8042_process_scode(i8042_device_t* dev, uint8_t scode, unsigned int flags) {
    // is this a multi code sequence?
    bool multi = (dev->last_code == 0xe0);

    // update the last received code
    dev->last_code = scode;

    // save the key up event bit
    bool key_up = !!(scode & 0x80);
    scode &= 0x7f;

    // translate the key based on our translation table
    uint8_t usage;
    if (multi) {
        usage = pc_set1_usage_map_e0[scode];
    } else {
        usage = pc_set1_usage_map[scode];
    }
    if (!usage) return;

    bool rollover = false;
    if (is_modifier(usage)) {
        switch (i8042_modifier(dev, usage, !key_up)) {
        case MOD_EXISTS:
            return;
        case MOD_ROLLOVER:
            rollover = true;
            break;
        case MOD_SET:
        default:
            break;
        }
    } else if (key_up) {
        if (i8042_rm_key(dev, usage) != KEY_REMOVED) {
            rollover = true;
        }
    } else {
        switch (i8042_add_key(dev, usage)) {
        case KEY_EXISTS:
            return;
        case KEY_ROLLOVER:
            rollover = true;
            break;
        case KEY_ADDED:
        default:
            break;
        }
    }

    //cprintf("i8042: scancode=0x%x, keyup=%u, multi=%u: usage=0x%x\n", scode, !!key_up, multi, usage);

    const boot_kbd_report_t* report = rollover ? &report_err_rollover : &dev->report;
    mxr_mutex_lock(&dev->fifo.lock);
    if (mx_hid_fifo_size(&dev->fifo) == 0) {
        device_state_set(&dev->device, DEV_STATE_READABLE);
    }
    mx_hid_fifo_write(&dev->fifo, (uint8_t*)report, sizeof(*report));
    mxr_mutex_unlock(&dev->fifo.lock);
}

static int i8042_irq_thread(void* arg) {
    i8042_device_t* device = (i8042_device_t*)arg;

    // enable I/O port access
    // TODO
    mx_status_t status;
    status = mx_mmap_device_io(I8042_COMMAND_REG, 1);
    if (status)
        return 0;
    status = mx_mmap_device_io(I8042_DATA_REG, 1);
    if (status)
        return 0;

    for (;;) {
        status = mx_interrupt_event_wait(device->irq);
        if (status == NO_ERROR) {
            // ack IRQ so we don't lose any IRQs that arrive while processing
            // (as this is an edge-triggered IRQ)
            mx_interrupt_event_complete(device->irq);

            // keep handling status on the keyboard controller until no bits are set we care about
            bool retry;
            do {
                retry = false;

                uint8_t str = i8042_read_status();

                // check for incoming data from the controller
                if (str & I8042_STR_OBF) {
                    uint8_t data = i8042_read_data();
                    i8042_process_scode(device, data,
                                        ((str & I8042_STR_PARITY) ? I8042_STR_PARITY : 0) |
                                            ((str & I8042_STR_TIMEOUT) ? I8042_STR_TIMEOUT : 0));
                    retry = true;
                }
                // TODO check other status bits here
            } while (retry);
        }
    }
    return 0;
}

static ssize_t i8042_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    size_t size = sizeof(boot_kbd_report_t);
    if (count < size || (count % size != 0))
        return ERR_INVALID_ARGS;

    i8042_device_t* device = get_kbd_device(dev);
    boot_kbd_report_t* data = (boot_kbd_report_t*)buf;
    mxr_mutex_lock(&device->fifo.lock);
    while (count > 0) {
        if (mx_hid_fifo_read(&device->fifo, (uint8_t*)data, size) < (ssize_t)size)
            break;
        data++;
        count -= size;
    }
    if (mx_hid_fifo_size(&device->fifo) == 0) {
        device_state_clr(dev, DEV_STATE_READABLE);
    }
    mxr_mutex_unlock(&device->fifo.lock);
    return (data - (boot_kbd_report_t*)buf) * size;
}

static ssize_t i8042_ioctl(mx_device_t* dev, uint32_t op, const void* in_buf, size_t in_len,
                           void* out_buf, size_t out_len) {
    switch (op) {
    case INPUT_IOCTL_GET_PROTOCOL: {
        if (out_len < sizeof(int)) return ERR_INVALID_ARGS;
        if (out_len < sizeof(int)) return ERR_INVALID_ARGS;
        int* reply = out_buf;
        *reply = INPUT_PROTO_KBD;
        return sizeof(*reply);
    }

    case INPUT_IOCTL_GET_REPORT_DESC_SIZE: {
        if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;
        size_t* reply = out_buf;
        *reply = sizeof(hid_report_desc);
        return sizeof(*reply);
    }

    case INPUT_IOCTL_GET_REPORT_DESC: {
        if (out_len < sizeof(hid_report_desc)) return ERR_INVALID_ARGS;
        memcpy(out_buf, &hid_report_desc, sizeof(hid_report_desc));
        return sizeof(hid_report_desc);
    }

    case INPUT_IOCTL_GET_NUM_REPORTS: {
        if (out_len < sizeof(size_t)) return ERR_INVALID_ARGS;
        size_t* reply = out_buf;
        *reply = 1;
        return sizeof(*reply);
    }

    case INPUT_IOCTL_GET_REPORT_IDS: {
        if (out_len < sizeof(input_report_id_t)) return ERR_INVALID_ARGS;
        input_report_id_t* reply = out_buf;
        *reply = 0;
        return sizeof(*reply);
    }

    case INPUT_IOCTL_GET_REPORT_SIZE:
    case INPUT_IOCTL_GET_MAX_REPORTSIZE: {
        if (out_len < sizeof(input_report_size_t)) return ERR_INVALID_ARGS;
        input_report_size_t* reply = out_buf;
        *reply = sizeof(boot_kbd_report_t);
        return sizeof(*reply);
    }
    }
    return ERR_NOT_SUPPORTED;
}

static mx_status_t i8042_release(mx_device_t* dev) {
    i8042_device_t* device = get_kbd_device(dev);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t i8042_device_proto = {
    .release = i8042_release,
    .read = i8042_read,
    .ioctl = i8042_ioctl,
};

static mx_status_t i8042_keyboard_init(mx_driver_t* driver) {
    // create device
    i8042_device_t* device = calloc(1, sizeof(i8042_device_t));
    if (!device)
        return ERR_NO_MEMORY;

    mx_hid_fifo_init(&device->fifo);

    mx_status_t status = device_init(&device->device, driver, "i8042-keyboard", &i8042_device_proto);
    if (status) {
        free(device);
        return status;
    }

    // add to root device
    device->device.protocol_id = MX_PROTOCOL_INPUT;
    if (device_add(&device->device, NULL)) {
        free(device);
        return NO_ERROR;
    }

    // enable I/O port access
    status = mx_mmap_device_io(I8042_COMMAND_REG, 1);
    if (status)
        goto fail;
    status = mx_mmap_device_io(I8042_DATA_REG, 1);
    if (status)
        goto fail;

    // initialize keyboard hardware
    i8042_flush();

    uint8_t ctr;
    if (i8042_command(&ctr, I8042_CMD_CTL_RCTR))
        goto fail;

    // turn on translation
    ctr |= I8042_CTR_XLATE;

    // enable keyboard and keyboard irq
    ctr &= ~I8042_CTR_KBDDIS;
    ctr |= I8042_CTR_KBDINT;

    if (i8042_command(&ctr, I8042_CMD_CTL_WCTR))
        goto fail;

    // enable PS/2 port
    i8042_command(NULL, I8042_CMD_KBD_EN);

    // send a enable scan command to the keyboard
    keyboard_command(&ctr, 0x1f4);

    // get interrupt wait handle
    device->irq = mx_interrupt_event_create(ISA_IRQ_KEYBOARD, MX_FLAG_REMAP_IRQ);
    if (device->irq < 0)
        goto fail;

    // create irq thread
    const char* name = "i8042-irq";
    status = mxr_thread_create(i8042_irq_thread, device, name, &device->irq_thread);
    if (status != NO_ERROR)
        goto fail;

    xprintf("initialized i8042_keyboard driver\n");

    return NO_ERROR;
fail:
    device_remove(&device->device);
    return status;
}

mx_driver_t _driver_i8042_keyboard BUILTIN_DRIVER = {
    .name = "i8042-keyboard",
    .ops = {
        .init = i8042_keyboard_init,
    },
};
