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
#include <ddk/binding.h>
#include <ddk/protocol/char.h>
#include <ddk/protocol/usb-device.h>
#include <ddk/protocol/keyboard.h>

#include <hw/usb.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HID_SUBCLASS_NONE 0
#define HID_SUBCLASS_BOOT 1

#define HID_PROTOCOL_BOOT 0
#define HID_PROTOCOL_REPORT 1

#define HID_BOOT_PROTOCOL_NONE 0
#define HID_BOOT_PROTOCOL_KEYBOARD 1
#define HID_BOOT_PROTOCOL_MOUSE 2

#define HID_GET_REPORT 1
#define HID_GET_IDLE 2
#define HID_GET_PROTOCOL 3
#define HID_SET_REPORT 9
#define HID_SET_IDLE 10
#define HID_SET_PROTOCOL 11

#define HID_L_CTL 0x01
#define HID_L_SHF 0x02
#define HID_L_ALT 0x04
#define HID_L_GUI 0x08
#define HID_R_CTL 0x10
#define HID_R_SHF 0x20
#define HID_R_ALT 0x40
#define HID_R_GUI 0x80

#define INTR_REQ_COUNT 8
#define INTR_REQ_SIZE 8

#define MAXKEYS 6

typedef struct {
    uint8_t mod;
    uint8_t reserved;
    uint8_t key[MAXKEYS];
} kbd_event_t;

typedef struct {
    mx_device_t dev;

    mx_device_t* usbdev;
    usb_device_protocol_t* usb;
    usb_endpoint_t* ept;
    usb_request_t* req;

    uint8_t mod;
    uint8_t key[MAXKEYS];

    uint32_t map[8];

    mx_key_fifo_t fifo;
} kbd_device_t;

static uint8_t modmap[8] = {
    MX_KEY_LCTRL, MX_KEY_LSHIFT, MX_KEY_LALT, MX_KEY_LWIN,
    MX_KEY_RCTRL, MX_KEY_RSHIFT, MX_KEY_RALT, MX_KEY_RWIN,
};

static uint8_t keymap[256] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
    'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
    '3', '4', '5', '6', '7', '8', '9', '0',
    MX_KEY_RETURN, MX_KEY_ESC, 8, MX_KEY_TAB, ' ', '-', '=', '[',
    ']', '\\', 0, ';', '\'', '`', ',', '.',
    '/', MX_KEY_CAPSLOCK, MX_KEY_F1, MX_KEY_F2,
    MX_KEY_F3, MX_KEY_F4, MX_KEY_F5, MX_KEY_F6,
    MX_KEY_F7, MX_KEY_F8, MX_KEY_F9, MX_KEY_F10,
    MX_KEY_F11, MX_KEY_F12, MX_KEY_PRTSCRN, MX_KEY_SCRLOCK,
    MX_KEY_PAUSE, MX_KEY_INS, MX_KEY_HOME, MX_KEY_PGUP,
    MX_KEY_DEL, MX_KEY_END, MX_KEY_PGDN, MX_KEY_ARROW_RIGHT,
    MX_KEY_ARROW_LEFT, MX_KEY_ARROW_DOWN, MX_KEY_ARROW_UP, MX_KEY_PAD_NUMLOCK,
    MX_KEY_PAD_DIVIDE, MX_KEY_PAD_MULTIPLY, MX_KEY_PAD_MINUS, MX_KEY_PAD_PLUS,
    MX_KEY_PAD_ENTER, MX_KEY_PAD_1, MX_KEY_PAD_2, MX_KEY_PAD_3,
    MX_KEY_PAD_4, MX_KEY_PAD_5, MX_KEY_PAD_6, MX_KEY_PAD_7,
    MX_KEY_PAD_8, MX_KEY_PAD_9, MX_KEY_PAD_0, MX_KEY_PAD_PERIOD,
};

static uint8_t keymap_shift[256] = {
    0, 0, 0, 0, 'A', 'B', 'C', 'D',
    'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
    '#', '$', '%', '^', '&', '*', '(', ')',
    MX_KEY_RETURN, MX_KEY_ESC, 8, MX_KEY_TAB, ' ', '_', '+', '{',
    '}', '|', 0, ':', '"', '~', '<', '>',
    '?', MX_KEY_CAPSLOCK, MX_KEY_F1, MX_KEY_F2,
    MX_KEY_F3, MX_KEY_F4, MX_KEY_F5, MX_KEY_F6,
    MX_KEY_F7, MX_KEY_F8, MX_KEY_F9, MX_KEY_F10,
    MX_KEY_F11, MX_KEY_F12, MX_KEY_PRTSCRN, MX_KEY_SCRLOCK,
    MX_KEY_PAUSE, MX_KEY_INS, MX_KEY_HOME, MX_KEY_PGUP,
    MX_KEY_DEL, MX_KEY_END, MX_KEY_PGDN, MX_KEY_ARROW_RIGHT,
    MX_KEY_ARROW_LEFT, MX_KEY_ARROW_DOWN, MX_KEY_ARROW_UP, MX_KEY_PAD_NUMLOCK,
    MX_KEY_PAD_DIVIDE, MX_KEY_PAD_MULTIPLY, MX_KEY_PAD_MINUS, MX_KEY_PAD_PLUS,
    MX_KEY_PAD_ENTER, MX_KEY_PAD_1, MX_KEY_PAD_2, MX_KEY_PAD_3,
    MX_KEY_PAD_4, MX_KEY_PAD_5, MX_KEY_PAD_6, MX_KEY_PAD_7,
    MX_KEY_PAD_8, MX_KEY_PAD_9, MX_KEY_PAD_0, MX_KEY_PAD_PERIOD,
};

#define KEYSET(bitmap,n) (bitmap[(n) >> 5] |= ((n) & 31))
#define KEYCLR(bitmap,n) (bitmap[(n) >> 5] &= (~((n) & 31)))
#define KEYTST(bitmap,n) (bitmap[(n) >> 5] & ((n) & 31))

#define get_kbd_device(dev) containerof(dev, kbd_device_t, dev)

static void kbd_queue_key(kbd_device_t* kbd, uint8_t key, bool pressed) {
    if (kbd->mod & (HID_L_SHF | HID_R_SHF)) {
        key = keymap_shift[key];
    } else {
        key = keymap[key];
    }
    if (key != 0) {
        mx_key_event_t ev;
        ev.pressed = pressed;
        ev.keycode = key;
        mxr_mutex_lock(&kbd->fifo.lock);
        if (kbd->fifo.head == kbd->fifo.tail) {
            device_state_set(&kbd->dev, DEV_STATE_READABLE);
        }
        mx_key_fifo_write(&kbd->fifo, &ev);
        mxr_mutex_unlock(&kbd->fifo.lock);
    }
}

static void kbd_process_event(kbd_device_t* kbd, const kbd_event_t* evt) {
    int i,j;
    for (i = 0; i < MAXKEYS; i++) {
        uint8_t k = evt->key[i];
        for (j = 0; j < MAXKEYS; j++) {
            if (kbd->key[j] == k) {
                kbd->key[j] = 0;
                goto key_still_down;
            }
        }
        KEYSET(kbd->map, k);
        kbd_queue_key(kbd, k, true);
key_still_down:
        ;
    }
    for (i = 0; i < MAXKEYS; i++) {
        if (kbd->key[i]) {
            // was down before, not still down
            KEYCLR(kbd->map, kbd->key[i]);
            kbd_queue_key(kbd, kbd->key[i], false);
        }
        kbd->key[i] = evt->key[i];
    }
    for (i = 0; i < 8; i++) {
        uint8_t bit = 1 << i;
        if (evt->mod & bit) {
            if (!(kbd->mod & bit)) {
                kbd_queue_key(kbd, modmap[i], true);
            }
        } else {
            if (kbd->mod & bit) {
                kbd_queue_key(kbd, modmap[i], false);
            }
        }
    }
    kbd->mod = evt->mod;
}

static void kbd_int_cb(usb_request_t* req) {
    kbd_device_t* kbd = (kbd_device_t*)req->client_data;
    if ((req->status == NO_ERROR) && (req->transfer_length == INTR_REQ_SIZE)) {
        kbd_process_event(kbd, (void*) req->buffer);
    }

    req->transfer_length = req->buffer_length;
    kbd->usb->queue_request(kbd->usbdev, req);
}

static ssize_t kbd_read(mx_device_t* dev, void* buf, size_t len, size_t off) {
    kbd_device_t* kbd = get_kbd_device(dev);
    mx_key_event_t* evt = buf;
    size_t count = 0;

    mxr_mutex_lock(&kbd->fifo.lock);
    while (len >= sizeof(mx_key_event_t)) {
        if (mx_key_fifo_read(&kbd->fifo, evt)) {
            break;
        }
        count += sizeof(mx_key_event_t);
        len -= sizeof(mx_key_event_t);
        evt++;
    }
    if (kbd->fifo.head == kbd->fifo.tail) {
        device_state_clr(&kbd->dev, DEV_STATE_READABLE);
    }
    mxr_mutex_unlock(&kbd->fifo.lock);
    return count;
}

static ssize_t kbd_write(mx_device_t* dev, const void* buf, size_t count, size_t off) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_char_t kbd_char_ops = {
    .read = kbd_read,
    .write = kbd_write,
};

static mx_status_t kbd_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t kbd_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t kbd_release(mx_device_t* dev) {
    free(dev);
    return NO_ERROR;
}

static mx_protocol_device_t kbd_dev_ops = {
    .get_protocol = device_base_get_protocol,
    .open = kbd_open,
    .close = kbd_close,
    .release = kbd_release,
};

static mx_status_t kbd_bind(mx_driver_t* drv, mx_device_t* dev) {
    kbd_device_t* kbd;
    if ((kbd = calloc(1, sizeof(kbd_device_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status = device_init(&kbd->dev, drv, "usb-keyboard", &kbd_dev_ops);
    if (status != NO_ERROR) {
        free(kbd);
        return status;
    }
    kbd->dev.protocol_id = MX_PROTOCOL_CHAR;
    kbd->dev.protocol_ops = &kbd_char_ops;

    kbd->usbdev = dev;
    if (device_get_protocol(dev, MX_PROTOCOL_USB_DEVICE, (void**)&kbd->usb) < 0) {
        free(kbd);
        return ERR_NOT_SUPPORTED;
    }

    usb_device_config_t* cfg;
    if (kbd->usb->get_config(dev, &cfg) < 0) {
        free(kbd);
        return ERR_NOT_SUPPORTED;
    }

    usb_configuration_t* config = &cfg->configurations[0];
    usb_interface_t* intf = &config->interfaces[0];
    usb_interface_descriptor_t* interface = intf->descriptor;

    for (int i = 0; i < intf->num_endpoints; i++) {
        if ((intf->endpoints[i].type == USB_ENDPOINT_INTERRUPT) &&
            (intf->endpoints[i].direction == USB_ENDPOINT_IN)) {
            kbd->ept = &intf->endpoints[i];
            goto found_endpoint;
        }
    }
    free(kbd);
    return ERR_NOT_SUPPORTED;

found_endpoint:
    if ((kbd->req = kbd->usb->alloc_request(kbd->usbdev, kbd->ept, INTR_REQ_SIZE)) == NULL) {
        free(kbd);
        return ERR_NO_MEMORY;
    }
    kbd->req->complete_cb = kbd_int_cb;
    kbd->req->client_data = kbd;

    kbd->usb->control(kbd->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                      HID_SET_PROTOCOL, HID_PROTOCOL_BOOT,
                      interface->bInterfaceNumber, NULL, 0);
    kbd->usb->control(kbd->usbdev, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE),
                      HID_SET_IDLE, 0,
                      interface->bInterfaceNumber, NULL, 0);

    kbd->fifo.lock = MXR_MUTEX_INIT;

    device_add(&kbd->dev, dev);

    kbd->req->transfer_length = kbd->req->buffer_length;
    kbd->usb->queue_request(kbd->usbdev, kbd->req);

    return NO_ERROR;
}

static mx_status_t kbd_unbind(mx_driver_t* drv, mx_device_t* dev) {
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_USB_DEVICE),
    BI_GOTO_IF(EQ, BIND_USB_CLASS, USB_CLASS_HID, 1),
    BI_ABORT_IF(NE, BIND_USB_CLASS, 0),
    BI_ABORT_IF(NE, BIND_USB_IFC_CLASS, USB_CLASS_HID),
    BI_LABEL(1),
    BI_ABORT_IF(NE, BIND_USB_IFC_SUBCLASS, HID_SUBCLASS_BOOT),
    BI_MATCH_IF(EQ, BIND_USB_IFC_PROTOCOL, HID_BOOT_PROTOCOL_KEYBOARD),
};

mx_driver_t _driver_usb_keyboard BUILTIN_DRIVER = {
    .name = "usb-keyboard",
    .ops = {
        .bind = kbd_bind,
        .unbind = kbd_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
