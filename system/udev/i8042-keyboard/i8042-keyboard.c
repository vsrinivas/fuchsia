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
#include <ddk/protocol/keyboard.h>
#include <hw/inout.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>

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

    bool key_lshift;
    bool key_rshift;
    int last_code;

    mx_key_fifo_t fifo;
} i8042_device_t;

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

/* scancode translation tables */
const uint8_t pc_keymap_set1_lower[128] = {
    /* 0x00 */ 0, MX_KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', MX_KEY_BACKSPACE, MX_KEY_TAB,
    /* 0x10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', MX_KEY_RETURN, MX_KEY_LCTRL, 'a', 's',
    /* 0x20 */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', MX_KEY_LSHIFT, '\\', 'z', 'x', 'c', 'v',
    /* 0x30 */ 'b', 'n', 'm', ',', '.', '/', MX_KEY_RSHIFT, '*', MX_KEY_LALT, ' ', MX_KEY_CAPSLOCK, MX_KEY_F1, MX_KEY_F2,
    MX_KEY_F3, MX_KEY_F4, MX_KEY_F5,
    /* 0x40 */ MX_KEY_F6, MX_KEY_F7, MX_KEY_F8, MX_KEY_F9, MX_KEY_F10, MX_KEY_PAD_NUMLOCK, MX_KEY_SCRLOCK, MX_KEY_PAD_7, MX_KEY_PAD_8,
    MX_KEY_PAD_9, MX_KEY_PAD_MINUS, MX_KEY_PAD_4, MX_KEY_PAD_5, MX_KEY_PAD_6, MX_KEY_PAD_PLUS, MX_KEY_PAD_1,
    /* 0x50 */ MX_KEY_PAD_2, MX_KEY_PAD_3, MX_KEY_PAD_0, MX_KEY_PAD_PERIOD, 0, 0, 0, MX_KEY_F11, MX_KEY_F12, 0, 0, 0, 0, 0, 0, 0,
};

const uint8_t pc_keymap_set1_upper[128] = {
    /* 0x00 */ 0, MX_KEY_ESC, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', MX_KEY_BACKSPACE, MX_KEY_TAB,
    /* 0x10 */ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', MX_KEY_RETURN, MX_KEY_LCTRL, 'A', 'S',
    /* 0x20 */ 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', MX_KEY_LSHIFT, '|', 'Z', 'X', 'C', 'V',
    /* 0x30 */ 'B', 'N', 'M', '<', '>', '?', MX_KEY_RSHIFT, '*', MX_KEY_LALT, ' ', MX_KEY_CAPSLOCK, MX_KEY_F1, MX_KEY_F2,
    MX_KEY_F3, MX_KEY_F4, MX_KEY_F5,
    /* 0x40 */ MX_KEY_F6, MX_KEY_F7, MX_KEY_F8, MX_KEY_F9, MX_KEY_F10, MX_KEY_PAD_NUMLOCK, MX_KEY_SCRLOCK, MX_KEY_PAD_7, MX_KEY_PAD_8,
    MX_KEY_PAD_9, MX_KEY_PAD_MINUS, MX_KEY_PAD_4, MX_KEY_PAD_5, MX_KEY_PAD_6, MX_KEY_PAD_PLUS, MX_KEY_PAD_1,
    /* 0x50 */ MX_KEY_PAD_2, MX_KEY_PAD_3, MX_KEY_PAD_0, MX_KEY_PAD_PERIOD, 0, 0, 0, MX_KEY_F11, MX_KEY_F12, 0, 0, 0, 0, 0, 0, 0,
};

const uint8_t pc_keymap_set1_e0[128] = {
    /* 0x00 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x10 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, MX_KEY_PAD_ENTER, MX_KEY_RCTRL, 0, 0,
    /* 0x20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x30 */ 0, 0, 0, 0, 0, MX_KEY_PAD_DIVIDE, 0, MX_KEY_PRTSCRN, MX_KEY_RALT, 0, 0, 0, 0, 0, 0, 0,
    /* 0x40 */ 0, 0, 0, 0, 0, 0, 0, MX_KEY_HOME, MX_KEY_ARROW_UP, MX_KEY_PGUP, 0, MX_KEY_ARROW_LEFT, 0, MX_KEY_ARROW_RIGHT, 0, MX_KEY_END,
    /* 0x50 */ MX_KEY_ARROW_DOWN, MX_KEY_PGDN, MX_KEY_INS, 0, 0, 0, 0, 0, 0, 0, 0, MX_KEY_LWIN, MX_KEY_RWIN, MX_KEY_MENU, 0, 0};

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
    uint8_t key_code;
    if (multi) {
        key_code = pc_keymap_set1_e0[scode];
    } else if (dev->key_lshift || dev->key_rshift) {
        key_code = pc_keymap_set1_upper[scode];
    } else {
        key_code = pc_keymap_set1_lower[scode];
    }

    if (key_code == MX_KEY_LSHIFT) {
        dev->key_lshift = !key_up;
    } else if (key_code == MX_KEY_RSHIFT) {
        dev->key_rshift = !key_up;
    }

    //cprintf("i8042: scancode=0x%x, keyup=%u, multi=%u: keycode=0x%x\n", scode, !!key_up, multi, key_code);

    mx_key_event_t ev = {.keycode = key_code, .pressed = !key_up};
    mxr_mutex_lock(&dev->fifo.lock);
    if (dev->fifo.head == dev->fifo.tail) {
        device_state_set(&dev->device, DEV_STATE_READABLE);
    }
    mx_key_fifo_write(&dev->fifo, &ev);
    mxr_mutex_unlock(&dev->fifo.lock);
}

static int i8042_irq_thread(void* arg) {
    i8042_device_t* device = (i8042_device_t*)arg;

    // enable I/O port access
    // TODO
    mx_status_t status;
    status = _magenta_mmap_device_io(I8042_COMMAND_REG, 1);
    if (status)
        return 0;
    status = _magenta_mmap_device_io(I8042_DATA_REG, 1);
    if (status)
        return 0;

    for (;;) {
        status = _magenta_interrupt_event_wait(device->irq);
        if (status == NO_ERROR) {
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
            _magenta_interrupt_event_complete(device->irq);
        }
    }
    return 0;
}

// implement char protocol:

static ssize_t i8042_read(mx_device_t* dev, void* buf, size_t count) {
    size_t size = sizeof(mx_key_event_t);
    if (count < size || (count % size != 0))
        return ERR_INVALID_ARGS;

    i8042_device_t* device = get_kbd_device(dev);
    mx_key_event_t* data = (mx_key_event_t*)buf;
    mxr_mutex_lock(&device->fifo.lock);
    while (count > 0) {
        if (mx_key_fifo_read(&device->fifo, data))
            break;
        data++;
        count -= size;
    }
    if (device->fifo.head == device->fifo.tail) {
        device_state_clr(dev, DEV_STATE_READABLE);
    }
    mxr_mutex_unlock(&device->fifo.lock);
    return (data - (mx_key_event_t*)buf) * size;
}

static ssize_t i8042_write(mx_device_t* dev, const void* buf, size_t count) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_char_t i8042_char_proto = {
    .read = i8042_read,
    .write = i8042_write,
};

// implement device protocol:

static mx_status_t i8042_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t i8042_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t i8042_release(mx_device_t* dev) {
    i8042_device_t* device = get_kbd_device(dev);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t i8042_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = i8042_open,
    .close = i8042_close,
    .release = i8042_release,
};

// implement driver object:

static mx_status_t i8042_keyboard_init(mx_driver_t* driver) {
    // create device
    i8042_device_t* device = calloc(1, sizeof(i8042_device_t));
    if (!device)
        return ERR_NO_MEMORY;

    device->fifo.lock = MXR_MUTEX_INIT;

    mx_status_t status = device_init(&device->device, driver, "i8042_keyboard", &i8042_device_proto);
    if (status) {
        free(device);
        return status;
    }

    // add to root device
    device->device.protocol_id = MX_PROTOCOL_CHAR;
    device->device.protocol_ops = &i8042_char_proto;
    if (device_add(&device->device, NULL)) {
        free(device);
        return NO_ERROR;
    }

    // enable I/O port access
    status = _magenta_mmap_device_io(I8042_COMMAND_REG, 1);
    if (status)
        goto fail;
    status = _magenta_mmap_device_io(I8042_DATA_REG, 1);
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
    device->irq = _magenta_interrupt_event_create(ISA_IRQ_KEYBOARD, MX_FLAG_REMAP_IRQ);
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
    .name = "i8042_keyboard",
    .ops = {
        .init = i8042_keyboard_init,
    },
};
