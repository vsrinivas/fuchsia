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

#include <ddk/protocol/char.h>
#include <ddk/protocol/console.h>
#include <font/font.h>
#include <magenta/syscalls.h>
#include <string.h>
#include <unistd.h>

#define VCDEBUG 1

#include "vc.h"
#include "vcdebug.h"

// implement char protocol:

ssize_t vc_char_read(mx_device_t* dev, void* buf, size_t count, size_t off) {
    vc_device_t* device = get_vc_device(dev);
    mx_key_event_t ev;
    ssize_t r = 0;
    mxr_mutex_lock(&device->fifo.lock);
    while (count > 0) {
        if (device->charcount > 0) {
            if (count > device->charcount) {
                count = device->charcount;
            }
            memcpy(buf, device->chardata, count);
            device->charcount -= count;
            if (device->charcount > 0) {
                memmove(device->chardata, device->chardata + count, device->charcount);
            }
            r = count;
            break;
        }
        if (mx_key_fifo_read(&device->fifo, &ev)) {
            // TODO: better error?
            r = 0;
            break;
        }

        char* str = device->chardata;
        if (ev.pressed) {
            switch (ev.keycode) {
            case MX_KEY_LSHIFT:
                device->modifiers |= MOD_LSHIFT;
                break;
            case MX_KEY_RSHIFT:
                device->modifiers |= MOD_RSHIFT;
                break;
            case MX_KEY_LCTRL:
                device->modifiers |= MOD_LCTRL;
                break;
            case MX_KEY_RCTRL:
                device->modifiers |= MOD_RCTRL;
                break;
            case MX_KEY_LALT:
                device->modifiers |= MOD_LALT;
                break;
            case MX_KEY_RALT:
                device->modifiers |= MOD_RALT;
                break;
            case 'a' ... 'z':
                if (device->modifiers & MOD_CTRL) {
                    str[0] = ev.keycode - 'a' + 1;
                } else {
                    str[0] = ev.keycode;
                }
                device->charcount = 1;
                break;
            case 'A' ... 'Z':
                if (device->modifiers & MOD_CTRL) {
                    str[0] = ev.keycode - 'A' + 1;
                } else {
                    str[0] = ev.keycode;
                }
                device->charcount = 1;
                break;

            // generate special stuff for a few different keys
            case MX_KEY_RETURN:
            case MX_KEY_PAD_ENTER:
                str[0] = '\n';
                device->charcount = 1;
                break;
            case MX_KEY_BACKSPACE:
                str[0] = '\b';
                device->charcount = 1;
                break;
            case MX_KEY_TAB:
                str[0] = '\t';
                device->charcount = 1;
                break;

            // generate vt100 key codes for arrows
            case MX_KEY_ARROW_UP:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 65;
                device->charcount = 3;
                break;
            case MX_KEY_ARROW_DOWN:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 66;
                device->charcount = 3;
                break;
            case MX_KEY_ARROW_RIGHT:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 67;
                device->charcount = 3;
                break;
            case MX_KEY_ARROW_LEFT:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 68;
                device->charcount = 3;
                break;

            default:
                if (ev.keycode < 0x80) {
                    str[0] = ev.keycode;
                    device->charcount = 1;
                }
                break;
            }
        } else {
            switch (ev.keycode) {
            case MX_KEY_LSHIFT:
                device->modifiers &= (~MOD_LSHIFT);
                break;
            case MX_KEY_RSHIFT:
                device->modifiers &= (~MOD_RSHIFT);
                break;
            case MX_KEY_LCTRL:
                device->modifiers &= (~MOD_LCTRL);
                break;
            case MX_KEY_RCTRL:
                device->modifiers &= (~MOD_RCTRL);
                break;
            case MX_KEY_LALT:
                device->modifiers &= (~MOD_LALT);
                break;
            case MX_KEY_RALT:
                device->modifiers &= (~MOD_RALT);
                break;
            }
        }
    }
    if ((device->fifo.head == device->fifo.tail) && (device->charcount == 0)) {
        device_state_clr(dev, DEV_STATE_READABLE);
    }
    mxr_mutex_unlock(&device->fifo.lock);
    return r;
}

ssize_t vc_char_write(mx_device_t* dev, const void* buf, size_t count, size_t off) {
    vc_device_t* device = get_vc_device(dev);
    mxr_mutex_lock(&device->lock);
    const uint8_t* str = (const uint8_t*)buf;
    for (size_t i = 0; i < count; i++) {
        device->textcon.putc(&device->textcon, str[i]);
    }
    if (!device->active && !(device->flags & VC_FLAG_HASINPUT)) {
        device->flags |= VC_FLAG_HASINPUT;
        vc_device_write_status(device);
        vc_gfx_invalidate(device, 0, 0, device->columns, 1);
    }
    mxr_mutex_unlock(&device->lock);
    return count;
}

ssize_t vc_char_ioctl(mx_device_t* dev, uint32_t op,
                      const void* cmd, size_t cmdlen,
                      void* reply, size_t max) {
    vc_device_t* device = get_vc_device(dev);
    switch (op) {
    case CONSOLE_OP_GET_DIMENSIONS: {
        ioctl_console_dimensions_t* dims = reply;
        if (sizeof(*dims) < max) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        dims->width = device->columns;
        dims->height = device->rows;
        return sizeof(*dims);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}
