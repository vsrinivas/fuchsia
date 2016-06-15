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
#include <ddk/protocol/display.h>
#include <ddk/protocol/keyboard.h>

#include <mxu/list.h>
#include <font/font.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <mxio/io.h>
#include <runtime/mutex.h>
#include <runtime/thread.h>

#define VCDEBUG 1

#include "vc.h"
#include "vcdebug.h"

static gfx_surface hw_gfx; // framebuffer
static mxr_thread_t* input_thread; // input wait thread
static mxr_thread_t* logreader_thread;

static struct list_node vc_list = LIST_INITIAL_VALUE(vc_list);
static vc_device_t* debug_vc;
static vc_device_t* active_vc;
static uint active_vc_index;
static uint vc_count;
static mxr_mutex_t active_lock = MXR_MUTEX_INIT;

// TODO create dynamically
#define VC_COUNT 4

// TODO need a better way to find the display/input devices
#if PROJECT_MAGENTA_QEMU_X86_64
static const char* input_dev = "/dev/protocol/char/i8042_keyboard";
#elif PROJECT_MAGENTA_PC_UEFI
static const char* input_dev = "/dev/protocol/char/i8042_keyboard";
#else
static const char* input_dev = NULL;
#endif

// TODO move this to ulib/gfx
static gfx_format display_format_to_gfx_format(uint display_format) {
    gfx_format format;
    switch (display_format) {
        case MX_DISPLAY_FORMAT_RGB_565:
            format = GFX_FORMAT_RGB_565;
            break;
        case MX_DISPLAY_FORMAT_RGB_332:
            format = GFX_FORMAT_RGB_332;
            break;
        case MX_DISPLAY_FORMAT_RGB_2220:
            format = GFX_FORMAT_RGB_2220;
            break;
        case MX_DISPLAY_FORMAT_ARGB_8888:
            format = GFX_FORMAT_ARGB_8888;
            break;
        case MX_DISPLAY_FORMAT_RGB_x888:
            format = GFX_FORMAT_RGB_x888;
            break;
        case MX_DISPLAY_FORMAT_MONO_8:
            format = GFX_FORMAT_MONO;
            break;
        default:
            xprintf("invalid graphics format)");
            return ERR_INVALID_ARGS;
    }
    return format;
}

static bool vc_ischar(mx_key_event_t* ev) {
    return ev->pressed && ((ev->keycode >= 1 && ev->keycode <= 0x7f) ||
                           ev->keycode == MX_KEY_RETURN ||
                           ev->keycode == MX_KEY_PAD_ENTER ||
                           ev->keycode == MX_KEY_BACKSPACE ||
                           ev->keycode == MX_KEY_TAB ||
                           (ev->keycode >= MX_KEY_ARROW_UP && ev->keycode <= MX_KEY_ARROW_LEFT));
}

static int vc_input_thread(void* arg) {
    int fd = open(input_dev, O_RDONLY);
    if (fd < 0) {
        printf("vc: cannot open '%s'\n", input_dev);
        return 0;
    }
    mx_key_event_t ev;
    int modifiers = 0;

    for (;;) {
        mxio_wait_fd(fd, MXIO_EVT_READABLE, NULL);
        int r = read(fd, &ev, sizeof(mx_key_event_t));
        if (r < 0) {
            return r;
        }
        if ((size_t)(r) != sizeof(mx_key_event_t)) {
            continue;
        }
        int consumed = 0;
        if (ev.pressed) {
            switch (ev.keycode) {
                // modifier keys are special
                case MX_KEY_LSHIFT:
                    modifiers |= MOD_LSHIFT;
                    break;
                case MX_KEY_RSHIFT:
                    modifiers |= MOD_RSHIFT;
                    break;
                case MX_KEY_LALT:
                    modifiers |= MOD_LALT;
                    break;
                case MX_KEY_RALT:
                    modifiers |= MOD_RALT;
                    break;
                case MX_KEY_LCTRL:
                    modifiers |= MOD_LCTRL;
                    break;
                case MX_KEY_RCTRL:
                    modifiers |= MOD_RCTRL;
                    break;

                case MX_KEY_F1:
                    vc_set_active_console(active_vc_index == 0 ? vc_count - 1 : active_vc_index - 1);
                    consumed = 1;
                    break;
                case MX_KEY_F2:
                    vc_set_active_console(active_vc_index == vc_count - 1 ? 0 : active_vc_index + 1);
                    consumed = 1;
                    break;

                case MX_KEY_ARROW_UP:
                    if (modifiers & MOD_LALT || modifiers & MOD_RALT) {
                        vc_device_scroll_viewport(active_vc, -1);
                        consumed = 1;
                    }
                    break;
                case MX_KEY_ARROW_DOWN:
                    if (modifiers & MOD_LALT || modifiers & MOD_RALT) {
                        vc_device_scroll_viewport(active_vc, 1);
                        consumed = 1;
                    }
                    break;

                // eat everything else
                default:
                    ; // nothing
            }
        } else {
            switch (ev.keycode) {
                // modifier keys are special
                case MX_KEY_LSHIFT:
                    modifiers &= ~MOD_LSHIFT;
                    break;
                case MX_KEY_RSHIFT:
                    modifiers &= ~MOD_RSHIFT;
                    break;
                case MX_KEY_LALT:
                    modifiers &= ~MOD_LALT;
                    break;
                case MX_KEY_RALT:
                    modifiers &= ~MOD_RALT;
                    break;
                case MX_KEY_LCTRL:
                    modifiers &= ~MOD_LCTRL;
                    break;
                case MX_KEY_RCTRL:
                    modifiers &= ~MOD_RCTRL;
                    break;

                default:
                    ; // nothing
            }
        }
        if (!consumed) {
            // TODO: decouple char device from actual device
            // TODO: ensure active vc can't change while this is going on
            mxr_mutex_lock(&active_vc->fifo.lock);
            if ((active_vc->fifo.head == active_vc->fifo.tail) && (active_vc->charcount == 0)) {
                active_vc->flags |= VC_FLAG_RESETSCROLL;
                device_state_set(&active_vc->device, DEV_STATE_READABLE);
            }
            mx_key_fifo_write(&active_vc->fifo, &ev);
            mxr_mutex_unlock(&active_vc->fifo.lock);
        }
    }
    return 0;
}

#define ESCAPE_HIDE_CURSOR "\033[?25l"

static int vc_logreader_thread(void* arg) {
    mx_handle_t h;

    if ((h = _magenta_log_create(MX_LOG_FLAG_CONSOLE)) < 0) {
        return h;
    }

    vc_device_t* vc = debug_vc;
    // hide cursor in logreader
    vc_char_write(&vc->device, ESCAPE_HIDE_CURSOR, strlen(ESCAPE_HIDE_CURSOR));

    char buf[MX_LOG_RECORD_MAX];
    mx_log_record_t* rec = (mx_log_record_t*) buf;
    for (;;) {
        if (_magenta_log_read(h, MX_LOG_RECORD_MAX, rec, MX_LOG_FLAG_WAIT) > 0) {
            char tmp[64];
            snprintf(tmp, 64, "[%05d.%03d] %c ",
                     (int) (rec->timestamp / 1000000000ULL),
                     (int) ((rec->timestamp / 1000000ULL) % 1000ULL),
                     (rec->flags & MX_LOG_FLAG_KERNEL) ? 'K' : 'U');
            vc_char_write(&vc->device, tmp, strlen(tmp));
            vc_char_write(&vc->device, rec->data, rec->datalen);
            if (rec->data[rec->datalen - 1] != '\n') {
                vc_char_write(&vc->device, "\n", 1);
            }
        }
    }
    return 0;
}

mx_status_t vc_set_active_console(uint console) {
    if (console >= vc_count) return ERR_INVALID_ARGS;

    uint i = 0;
    vc_device_t* device = NULL;
    list_for_every_entry(&vc_list, device, vc_device_t, node) {
        if (i == console) break;
        i++;
    }
    if (device == active_vc) return NO_ERROR;
    mxr_mutex_lock(&active_lock);
    if (active_vc) active_vc->active = false;
    device->active = true;
    active_vc = device;
    active_vc->flags &= ~VC_FLAG_HASINPUT;
    active_vc_index = console;
    mxr_mutex_unlock(&active_lock);
    vc_device_render(active_vc);
    return NO_ERROR;
}

void vc_get_status_line(char* str, int n) {
    vc_device_t* device = NULL;
    char* ptr = str;
    uint i = 0;
    // TODO add process name, etc.
    list_for_every_entry(&vc_list, device, vc_device_t, node) {
        int lines = vc_device_get_scrollback_lines(device);
        int chars = snprintf(ptr, n, "%s[%u] %s%c    %c%c \033[m",
            device->active ? "\033[36m\033[1m" : "",
            i,
            device->title,
            device->flags & VC_FLAG_HASINPUT ? '*' : ' ',
            lines > 0 && -device->vpy < lines ? '<' : ' ',
            device->vpy < 0 ? '>' : ' ');
        ptr += chars;
        i++;
    }
}

static mx_protocol_device_t vc_device_proto = {
    .get_protocol = vc_device_get_protocol,
    .open = vc_device_open,
    .close = vc_device_close,
    .release = vc_device_release,
};

static mx_driver_t _driver_vc = {
    .name = "vc",
    .ops = {
    },
};

static mx_status_t vc_root_bind(mx_driver_t* drv, mx_device_t* dev) {
    mx_status_t status;

    mx_display_protocol_t* disp;
    if ((status = device_get_protocol(dev, MX_PROTOCOL_DISPLAY, (void**)&disp)) < 0) {
        return status;
    }

    // get display info
    mx_display_info_t info;
    if ((status = disp->get_mode(dev, &info)) < 0) {
        return status;
    }

    // get framebuffer
    void* framebuffer;
    if ((status = disp->get_framebuffer(dev, &framebuffer)) < 0) {
        return status;
    }

    // get display format
    gfx_format format = display_format_to_gfx_format(info.format);

    // initialize the hw surface
    if ((status = gfx_init_surface(&hw_gfx, framebuffer, info.width, info.height, info.stride, format, 0)) < 0) {
        return status;
    }

    uint i;
    for (i = 0; i < VC_COUNT; i++) {
        // allocate vc# devices
        vc_device_t* device;
        if ((status = vc_device_alloc(&hw_gfx, &device)) < 0) {
            break;
        }

        // init the vc device
        char name[4];
        snprintf(name, sizeof(name), "vc%u", i);
        if ((status = device_init(&device->device, &_driver_vc, name, &vc_device_proto)) < 0) {
            break;
        }
        if (i == 0) {
            strncpy(device->title, "syslog", sizeof(device->title));
        } else {
            strncpy(device->title, name, sizeof(device->title));
        }
        device->device.protocol_id = MX_PROTOCOL_CHAR;

        // add devices to root node
        list_add_tail(&vc_list, &device->node);
        device_add(&device->device, dev);
    }
    if (i == 0) {
        // TODO: cleanup surface and thread
        return status;
    }
    vc_count = i;
    vc_set_active_console(0);
    // vc0 is the debug console
    debug_vc = active_vc;

    xprintf("initialized vc on display %s, width=%u height=%u stride=%u format=%u, count=%u\n", dev->name, info.width, info.height, info.stride, format, i);

    // start a thread to wait for input
    if ((status = mxr_thread_create(vc_input_thread, NULL, "vc-input", &input_thread)) < 0) {
        printf("vc-input thread did not start %d\n", status);
    }

    mxr_thread_create(vc_logreader_thread, NULL, "vc-debuglog", &logreader_thread);

    return NO_ERROR;
}

static mx_status_t vc_root_probe(mx_driver_t* drv, mx_device_t* dev) {
    if (vc_count > 0) {
        // disallow multiple instances
        return ERR_NOT_SUPPORTED;
    }
    // TODO: bind by protocol
    if (strcmp(dev->name, "bochs_vbe") && strcmp(dev->name, "intel_i915_disp")) {
        return ERR_NOT_SUPPORTED;
    }
    return NO_ERROR;
}

static mx_driver_binding_t binding = {
    .protocol_id = MX_PROTOCOL_DISPLAY,
};

mx_driver_t _driver_vc_root BUILTIN_DRIVER = {
    .name = "vc-root",
    .ops = {
        .probe = vc_root_probe,
        .bind = vc_root_bind,
    },
    .binding = &binding,
    .binding_count = 1,
};
