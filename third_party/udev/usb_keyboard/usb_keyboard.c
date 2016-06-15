/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2008-2010 coresystems GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define USB_DEBUG

#include <hw/usb.h>
#include <inttypes.h>
#include <magenta/types.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/char.h>
#include <ddk/protocol/usb_device.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define KEY_BREAK 0x101
#define KEY_DOWN 0x102
#define KEY_UP 0x103
#define KEY_LEFT 0x104
#define KEY_RIGHT 0x105
#define KEY_HOME 0x106
#define KEY_F1 0x109
#define KEY_F2 0x10A
#define KEY_F3 0x10B
#define KEY_F4 0x10C
#define KEY_F5 0x10D
#define KEY_F6 0x10E
#define KEY_F7 0x10F
#define KEY_F8 0x110
#define KEY_F9 0x111
#define KEY_F10 0x112
#define KEY_F11 0x113
#define KEY_F12 0x114
#define KEY_DC 0x14A
#define KEY_IC 0x14B
#define KEY_NPAGE 0x152
#define KEY_PPAGE 0x153
#define KEY_ENTER 0x157
#define KEY_PRINT 0x15A
#define KEY_END 0x166

#define INTR_REQ_COUNT 8
#define INTR_REQ_SIZE 8

enum { hid_subclass_none = 0,
       hid_subclass_boot = 1 };
typedef enum { hid_proto_boot = 0,
               hid_proto_report = 1 } hid_proto;
enum { hid_boot_proto_none = 0,
       hid_boot_proto_keyboard =
           1,
       hid_boot_proto_mouse = 2
};
enum { GET_REPORT = 0x1,
       GET_IDLE = 0x2,
       GET_PROTOCOL = 0x3,
       SET_REPORT =
           0x9,
       SET_IDLE = 0xa,
       SET_PROTOCOL = 0xb
};

typedef union {
    struct {
        uint8_t modifiers;
        uint8_t repeats;
        uint8_t keys[6];
    };
    uint8_t buffer[8];
} usb_hid_keyboard_event_t;

typedef struct {
    mx_device_t device;
    mx_device_t* usb_device;
    usb_device_protocol_t* device_protocol;

    usb_endpoint_t* intr_ep;
    usb_hid_descriptor_t descriptor;

    usb_hid_keyboard_event_t previous;
    int lastkeypress;
    int repeat_delay;

    list_node_t free_intr_reqs;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    // buffer for char driver read
    char key_buffer[10];
    // index of first char in key_buffer
    int key_buffer_head;
    // number of chars in key_buffer
    size_t key_buffer_count;
} kbd_device_t;
#define get_kbd_device(dev) containerof(dev, kbd_device_t, device)

const char* countries[36][2] = {
    {"not supported", "us"},
    {"Arabic", "ae"},
    {"Belgian", "be"},
    {"Canadian-Bilingual", "ca"},
    {"Canadian-French", "ca"},
    {"Czech Republic", "cz"},
    {"Danish", "dk"},
    {"Finnish", "fi"},
    {"French", "fr"},
    {"German", "de"},
    {"Greek", "gr"},
    {"Hebrew", "il"},
    {"Hungary", "hu"},
    {"International (ISO)", "iso"},
    {"Italian", "it"},
    {"Japan (Katakana)", "jp"},
    {"Korean", "us"},
    {"Latin American", "us"},
    {"Netherlands/Dutch", "nl"},
    {"Norwegian", "no"},
    {"Persian (Farsi)", "ir"},
    {"Poland", "pl"},
    {"Portuguese", "pt"},
    {"Russia", "ru"},
    {"Slovakia", "sl"},
    {"Spanish", "es"},
    {"Swedish", "se"},
    {"Swiss/French", "ch"},
    {"Swiss/German", "ch"},
    {"Switzerland", "ch"},
    {"Taiwan", "tw"},
    {"Turkish-Q", "tr"},
    {"UK", "uk"},
    {"US", "us"},
    {"Yugoslavia", "yu"},
    {"Turkish-F", "tr"},
    /* 36 - 255: Reserved */
};

struct layout_maps {
    const char* country;
    const short map[4][0x80];
};

static const struct layout_maps* map;

#define KEY_F1 0x109
#define KEY_F2 0x10A
#define KEY_F3 0x10B
#define KEY_F4 0x10C
#define KEY_F5 0x10D
#define KEY_F6 0x10E
#define KEY_F7 0x10F
#define KEY_F8 0x110
#define KEY_F9 0x111
#define KEY_F10 0x112
#define KEY_F11 0x113
#define KEY_F12 0x114

static const struct layout_maps keyboard_layouts[] = {
    // #if IS_ENABLED(CONFIG_LP_PC_KEYBOARD_LAYOUT_US)
    {.country = "us", .map = {{
                               /* No modifier */
                               -1, -1, -1, -1, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
                               /* 0x10 */
                               'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
                               /* 0x20 */
                               '3', '4', '5', '6', '7', '8', '9', '0', '\n', '\e', '\b', '\t', ' ', '-', '=', '[',
                               /* 0x30 */
                               ']', '\\', -1, ';', '\'', '`', ',', '.', '/', -1 /* CapsLk */, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
                               /* 0x40 */
                               KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRINT, -1 /* ScrLk */, KEY_BREAK, KEY_IC, KEY_HOME, KEY_PPAGE, KEY_DC, KEY_END, KEY_NPAGE, KEY_RIGHT,
                               /* 50 */
                               KEY_LEFT, KEY_DOWN, KEY_UP, -1 /*NumLck*/, '/', '*', '-' /* = ? */, '+', KEY_ENTER, KEY_END, KEY_DOWN, KEY_NPAGE, KEY_LEFT, -1, KEY_RIGHT, KEY_HOME,
                               /* 60 */
                               KEY_UP, KEY_PPAGE, -1, KEY_DC, -1 /* < > | */, -1 /* Win Key Right */, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               /* 70 */
                               -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                              },
                              {
                               /* Shift modifier */
                               -1, -1, -1, -1, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
                               /* 0x10 */
                               'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
                               /* 0x20 */
                               '#', '$', '%', '^', '&', '*', '(', ')', '\n', '\e', '\b', '\t', ' ', '_', '+', '[',
                               /* 0x30 */
                               ']', '\\', -1, ':', '\'', '`', ',', '.', '/', -1 /* CapsLk */, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
                               /* 0x40 */
                               KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRINT, -1 /* ScrLk */, KEY_BREAK, KEY_IC, KEY_HOME, KEY_PPAGE, KEY_DC, KEY_END, KEY_NPAGE, KEY_RIGHT,
                               /* 50 */
                               KEY_LEFT, KEY_DOWN, KEY_UP, -1 /*NumLck*/, '/', '*', '-' /* = ? */, '+', KEY_ENTER, KEY_END, KEY_DOWN, KEY_NPAGE, KEY_LEFT, -1, KEY_RIGHT, KEY_HOME,
                               /* 60 */
                               KEY_UP, KEY_PPAGE, -1, KEY_DC, -1 /* < > | */, -1 /* Win Key Right */, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               /* 70 */
                               -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                              },
                              {
                               /* Alt */
                               -1, -1, -1, -1, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
                               /* 0x10 */
                               'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
                               /* 0x20 */
                               '3', '4', '5', '6', '7', '8', '9', '0', '\n', '\e', '\b', '\t', ' ', '-', '=', '[',
                               /* 0x30 */
                               ']', '\\', -1, ';', '\'', '`', ',', '.', '/', -1 /* CapsLk */, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
                               /* 0x40 */
                               KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRINT, -1 /* ScrLk */, KEY_BREAK, KEY_IC, KEY_HOME, KEY_PPAGE, KEY_DC, KEY_END, KEY_NPAGE, KEY_RIGHT,
                               /* 50 */
                               KEY_LEFT, KEY_DOWN, KEY_UP, -1 /*NumLck*/, '/', '*', '-' /* = ? */, '+', KEY_ENTER, KEY_END, KEY_DOWN, KEY_NPAGE, KEY_LEFT, -1, KEY_RIGHT, KEY_HOME,
                               /* 60 */
                               KEY_UP, KEY_PPAGE, -1, KEY_DC, -1 /* < > | */, -1 /* Win Key Right */, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               /* 70 */
                               -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                              },
                              {
                               /* Shift+Alt modifier */
                               -1, -1, -1, -1, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
                               /* 0x10 */
                               'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
                               /* 0x20 */
                               '#', '$', '%', '^', '&', '*', '(', ')', '\n', '\e', '\b', '\t', ' ', '-', '=', '[',
                               /* 0x30 */
                               ']', '\\', -1, ':', '\'', '`', ',', '.', '/', -1 /* CapsLk */, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
                               /* 0x40 */
                               KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, KEY_PRINT, -1 /* ScrLk */, KEY_BREAK, KEY_IC, KEY_HOME, KEY_PPAGE, KEY_DC, KEY_END, KEY_NPAGE, KEY_RIGHT,
                               /* 50 */
                               KEY_LEFT, KEY_DOWN, KEY_UP, -1 /*NumLck*/, '/', '*', '-' /* = ? */, '+', KEY_ENTER, KEY_END, KEY_DOWN, KEY_NPAGE, KEY_LEFT, -1, KEY_RIGHT, KEY_HOME,
                               /* 60 */
                               KEY_UP, KEY_PPAGE, -1, KEY_DC, -1 /* < > | */, -1 /* Win Key Right */, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               /* 70 */
                               -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                              }}},
    //#endif
};

#define MOD_SHIFT (1 << 0)
#define MOD_ALT (1 << 1)
#define MOD_CTRL (1 << 2)

static void usb_hid_keyboard_queue(kbd_device_t* kbd, int ch) {
    printf("got char: %c\n", ch);
    pthread_mutex_lock(&kbd->mutex);

    int index;
    if (kbd->key_buffer_count == 0) {
        index = 0;
    } else {
        index = (kbd->key_buffer_head + kbd->key_buffer_count) % countof(kbd->key_buffer);
    }
    kbd->key_buffer[index] = ch;

    if (kbd->key_buffer_count < countof(kbd->key_buffer)) {
        kbd->key_buffer_count++;
    } else {
        // we overflowed the buffer, so increment our head to the oldest char in the buffer
        kbd->key_buffer_head = (kbd->key_buffer_head + 1) % countof(kbd->key_buffer);
    }
    pthread_cond_signal(&kbd->cond);

    pthread_mutex_unlock(&kbd->mutex);
}

#define KEYBOARD_REPEAT_MS 30
#define INITIAL_REPEAT_DELAY 10
#define REPEAT_DELAY 2

static void
usb_hid_process_keyboard_event(kbd_device_t* kbd,
                               const usb_hid_keyboard_event_t* const current) {
    const usb_hid_keyboard_event_t* const previous = &kbd->previous;

    int i, keypress = 0, modifiers = 0;

    if (current->modifiers & 0x01) /* Left-Ctrl */
        modifiers |= MOD_CTRL;
    if (current->modifiers & 0x02) /* Left-Shift */
        modifiers |= MOD_SHIFT;
    if (current->modifiers & 0x04) /* Left-Alt */
        modifiers |= MOD_ALT;
    //    if (current->modifiers & 0x08) /* Left-GUI */
    //        ;
    if (current->modifiers & 0x10) /* Right-Ctrl */
        modifiers |= MOD_CTRL;
    if (current->modifiers & 0x20) /* Right-Shift */
        modifiers |= MOD_SHIFT;
    if (current->modifiers & 0x40) /* Right-AltGr */
        modifiers |= MOD_ALT;
    //    if (current->modifiers & 0x80) /* Right-GUI */
    //        ;

    if ((current->modifiers & 0x05) && ((current->keys[0] == 0x4c) ||
                                        (current->keys[0] == 0x63))) {
        /* vulcan nerve pinch */
        //		if (reset_handler)
        //			reset_handler();
    }

    /* Did the event change at all? */
    if (kbd->lastkeypress &&
        !memcmp(current, previous, sizeof(*current))) {
        /* No. Then it's a key repeat event. */
        if (kbd->repeat_delay) {
            kbd->repeat_delay--;
        } else {
            usb_hid_keyboard_queue(kbd, kbd->lastkeypress);
            kbd->repeat_delay = REPEAT_DELAY;
        }

        return;
    }

    kbd->lastkeypress = 0;

    for (i = 0; i < 6; i++) {
        int j;
        int skip = 0;
        // No more keys? skip
        if (current->keys[i] == 0)
            return;

        for (j = 0; j < 6; j++) {
            if (current->keys[i] == previous->keys[j]) {
                skip = 1;
                break;
            }
        }
        if (skip)
            continue;

        /* Mask off MOD_CTRL */
        keypress = map->map[modifiers & 0x03][current->keys[i]];

        if (modifiers & MOD_CTRL) {
            switch (keypress) {
            case 'a' ... 'z':
                keypress &= 0x1f;
                break;
            default:
                continue;
            }
        }

        if (keypress == -1) {
            /* Debug: Print unknown keys */
            printf("usbhid: <%x> %x [ %x %x %x %x %x %x ] %d\n",
                   current->modifiers, current->repeats,
                   current->keys[0], current->keys[1],
                   current->keys[2], current->keys[3],
                   current->keys[4], current->keys[5], i);

            /* Unknown key? Try next one in the queue */
            continue;
        }

        usb_hid_keyboard_queue(kbd, keypress);

        /* Remember for authentic key repeat */
        kbd->lastkeypress = keypress;
        kbd->repeat_delay = INITIAL_REPEAT_DELAY;
    }
}

static void usb_keyboard_interrupt(const void* data, size_t length, void* context) {
    if (length != 8) {
        printf("usb_keyboard_interrupt: unexpected packet length %" PRIuPTR "\n", length);
        return;
    }
    kbd_device_t* kbd = (kbd_device_t*)context;
    usb_hid_keyboard_event_t current;
    memcpy(&current.buffer, data, 8);
    usb_hid_process_keyboard_event(kbd, &current);
    kbd->previous = current;
}

static void queue_interrupt_requests_locked(kbd_device_t* kbd) {
    list_node_t* node;
    while ((node = list_remove_head(&kbd->free_intr_reqs)) != NULL) {
        usb_request_t* req = containerof(node, usb_request_t, node);
        req->transfer_length = req->buffer_length;
        mx_status_t status = kbd->device_protocol->queue_request(kbd->usb_device, req);
        if (status != NO_ERROR) {
            printf("interrupt queue failed %d\n", status);
            list_add_head(&kbd->free_intr_reqs, &req->node);
            break;
        }
    }
}

static void usb_keyboard_interrupt_complete(usb_request_t* request) {
    kbd_device_t* kbd = (kbd_device_t*)request->client_data;

    if (request->status == NO_ERROR && request->transfer_length == INTR_REQ_SIZE) {
        usb_hid_keyboard_event_t current;
        memcpy(&current.buffer, request->buffer, INTR_REQ_SIZE);
        usb_hid_process_keyboard_event(kbd, &current);
        kbd->previous = current;
    }

    pthread_mutex_lock(&kbd->mutex);
    list_add_head(&kbd->free_intr_reqs, &request->node);
    queue_interrupt_requests_locked(kbd);
    pthread_mutex_unlock(&kbd->mutex);
}

static int usb_hid_set_layout(const char* country) {
    /* FIXME should be per keyboard */
    size_t i;

    for (i = 0; i < ARRAY_SIZE(keyboard_layouts); i++) {
        if (strncmp(keyboard_layouts[i].country, country,
                    strlen(keyboard_layouts[i].country)))
            continue;

        /* Found, changing keyboard layout */
        map = &keyboard_layouts[i];
        printf("  Keyboard layout '%s'\n", map->country);
        return 0;
    }

    printf("  Keyboard layout '%s' not found, using '%s'\n",
           country, map->country);

    /* Nothing found, not changed */
    return -1;
}

static mx_status_t usb_keyboard_probe(mx_driver_t* driver, mx_device_t* device) {
    usb_device_protocol_t* protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&protocol)) {
        return ERR_NOT_SUPPORTED;
    }

    usb_device_config_t* device_config;
    mx_status_t status = protocol->get_config(device, &device_config);
    if (status < 0)
        return status;

    usb_configuration_t* config = &device_config->configurations[0];
    usb_interface_t* intf = &config->interfaces[0];
    usb_interface_descriptor_t* interface = intf->descriptor;

    if (!(device_config->descriptor->bDeviceClass == USB_CLASS_HID ||
          (device_config->descriptor->bDeviceClass == 0 && interface->bInterfaceClass == USB_CLASS_HID)))
        return ERR_NOT_SUPPORTED;
    if (interface->bInterfaceSubClass != hid_subclass_boot)
        return ERR_NOT_SUPPORTED;
    if (interface->bInterfaceProtocol != hid_boot_proto_keyboard)
        return ERR_NOT_SUPPORTED;
    return NO_ERROR;
}

static ssize_t usb_keyboard_read(mx_device_t* dev, void* buf, size_t count) {
    char* buffer = buf;
    kbd_device_t* kbd = get_kbd_device(dev);

    pthread_mutex_lock(&kbd->mutex);

    while (kbd->key_buffer_count == 0) {
        pthread_cond_wait(&kbd->cond, &kbd->mutex);
    }

    if (count > kbd->key_buffer_count) {
        count = kbd->key_buffer_count;
    }

    for (size_t i = 0; i < count; i++) {
        *buffer++ = kbd->key_buffer[kbd->key_buffer_head++];
        if (kbd->key_buffer_head == countof(kbd->key_buffer)) {
            kbd->key_buffer_head = 0;
        }
    }

    kbd->key_buffer_count -= count;

    pthread_mutex_unlock(&kbd->mutex);

    return count;
}

static ssize_t usb_keyboard_write(mx_device_t* dev, const void* buf, size_t count) {
    return ERR_NOT_SUPPORTED;
}

static mx_protocol_char_t usb_keyboard_char_proto = {
    .read = usb_keyboard_read,
    .write = usb_keyboard_write,
};

static mx_status_t usb_keyboard_open(mx_device_t* dev, uint32_t flags) {
    return NO_ERROR;
}

static mx_status_t usb_keyboard_close(mx_device_t* dev) {
    return NO_ERROR;
}

static mx_status_t usb_keyboard_release(mx_device_t* device) {
    kbd_device_t* kbd = get_kbd_device(device);
    pthread_mutex_destroy(&kbd->mutex);
    pthread_cond_destroy(&kbd->cond);
    free(kbd);

    return NO_ERROR;
}

static mx_protocol_device_t usb_keyboard_device_proto = {
    .get_protocol = device_base_get_protocol,
    .open = usb_keyboard_open,
    .close = usb_keyboard_close,
    .release = usb_keyboard_release,
};

static mx_status_t usb_keyboard_bind(mx_driver_t* driver, mx_device_t* device) {
    kbd_device_t* kbd = calloc(1, sizeof(kbd_device_t));
    if (!kbd) {
        printf("Not enough memory for USB HID device.\n");
        return ERR_NO_MEMORY;
    }

    list_initialize(&kbd->free_intr_reqs);
    pthread_mutex_init(&kbd->mutex, NULL);
    pthread_cond_init(&kbd->cond, NULL);

    mx_status_t status = device_init(&kbd->device, driver, "usb_keyboard", &usb_keyboard_device_proto);
    if (status != NO_ERROR) {
        free(kbd);
        return status;
    }
    kbd->device.protocol_id = MX_PROTOCOL_CHAR;
    kbd->device.protocol_ops = &usb_keyboard_char_proto;

    // FIXME - free kbd if errors occur

    usb_device_protocol_t* protocol;
    if (device_get_protocol(device, MX_PROTOCOL_USB_DEVICE, (void**)&protocol)) {
        free(kbd);
        return ERR_NOT_SUPPORTED;
    }
    kbd->usb_device = device;
    kbd->device_protocol = protocol;

    usb_device_config_t* device_config;
    status = protocol->get_config(device, &device_config);
    if (status < 0)
        return status;

    usb_configuration_t* config = &device_config->configurations[0];
    usb_interface_t* intf = &config->interfaces[0];
    usb_interface_descriptor_t* interface = intf->descriptor;

    printf("  configuring...\n");
    protocol->control(device, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE), SET_PROTOCOL,
                      hid_proto_boot, interface->bInterfaceNumber, NULL, 0);
    protocol->control(device, (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE), SET_IDLE,
                      (KEYBOARD_REPEAT_MS >> 2) << 8, interface->bInterfaceNumber, NULL, 0);
    printf("  activating...\n");

    if (protocol->control(device, (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE), USB_REQ_GET_DESCRIPTOR,
                          (0x21 << 8 | 0), 0, &kbd->descriptor, sizeof(kbd->descriptor) != sizeof(kbd->descriptor))) {
        printf("get_descriptor(HID) failed\n");
        return ERR_GENERIC;
    }
    unsigned int countrycode = kbd->descriptor.bCountryCode;
    /* 35 countries defined: */
    if (countrycode >= ARRAY_SIZE(countries))
        countrycode = 0;
    printf("  Keyboard has %s layout (country code %02x)\n",
           countries[countrycode][0], countrycode);

    /* Set keyboard layout accordingly */
    usb_hid_set_layout(countries[countrycode][1]);

    int i;
    for (i = 0; i < intf->num_endpoints; i++) {
        if (intf->endpoints[i].type != USB_ENDPOINT_INTERRUPT)
            continue;
        if (intf->endpoints[i].direction != USB_ENDPOINT_IN)
            continue;
        break;
    }
    if (i >= intf->num_endpoints) {
        printf("Could not find HID endpoint\n");
        return ERR_GENERIC;
    }
    printf("  found endpoint %x for interrupt-in\n", i);
    /* 20 buffers of 8 bytes, for every 10 msecs */
    kbd->intr_ep = &intf->endpoints[i];
    printf("  configuration done.\n");

    for (int i = 0; i < INTR_REQ_COUNT; i++) {
        usb_request_t* req = protocol->alloc_request(device, kbd->intr_ep, INTR_REQ_SIZE);
        if (!req)
            return ERR_NO_MEMORY;
        req->complete_cb = usb_keyboard_interrupt_complete;
        req->client_data = kbd;
        list_add_head(&kbd->free_intr_reqs, &req->node);
    }

    pthread_mutex_lock(&kbd->mutex);
    queue_interrupt_requests_locked(kbd);
    pthread_mutex_unlock(&kbd->mutex);

    printf("kbd add %s to %s\n", kbd->device.name, device->name);
    device_add(&kbd->device, device);

    return NO_ERROR;
}

static mx_status_t usb_keyboard_unbind(mx_driver_t* drv, mx_device_t* dev) {
    mx_device_t* child = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe (&dev->device_list, child, temp, mx_device_t, node) {
        device_remove(child);
    }
    return NO_ERROR;
}

static mx_driver_binding_t binding = {
    .protocol_id = MX_PROTOCOL_USB_DEVICE,
};

mx_driver_t _driver_usb_keyboard BUILTIN_DRIVER = {
    .name = "usb_keyboard",
    .ops = {
        .probe = usb_keyboard_probe,
        .bind = usb_keyboard_bind,
        .unbind = usb_keyboard_unbind,
    },
    .binding = &binding,
    .binding_count = 1,
};
