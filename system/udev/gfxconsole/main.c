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
#include <ddk/binding.h>
#include <ddk/protocol/console.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/input.h>

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <gfx/gfx.h>
#include <hid/hid.h>
#include <hid/usages.h>
#include <system/listnode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mxio/io.h>
#include <runtime/mutex.h>
#include <runtime/thread.h>

#include <magenta/syscalls-ddk.h>

#define VCDEBUG 1

#include "vc.h"
#include "vcdebug.h"

#define VC_DEVNAME "vc"

// framebuffer
static gfx_surface hw_gfx;

#define INPUT_LISTENER_FLAG_RUNNING 1

typedef struct {
    char dev_name[128];
    mxr_thread_t* t;
    int flags;
    int fd;
    struct list_node node;
} input_listener_t;

static struct list_node input_listeners_list = LIST_INITIAL_VALUE(input_listeners_list);
static mxr_thread_t* input_poll_thread;

// single driver instance
static bool vc_initialized = false;

static struct list_node vc_list = LIST_INITIAL_VALUE(vc_list);
static unsigned vc_count = 0;
static vc_device_t* active_vc;
static unsigned active_vc_index;
static mxr_mutex_t vc_lock = MXR_MUTEX_INIT;

// TODO move this to ulib/gfx
static gfx_format display_format_to_gfx_format(unsigned display_format) {
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

static int vc_input_thread(void* arg) {
    input_listener_t* listener = arg;
    assert(listener->flags & INPUT_LISTENER_FLAG_RUNNING);
    assert(listener->fd >= 0);
    xprintf("vc: input thread started for %s\n", listener->dev_name);

    uint8_t report_buf[9];
    uint8_t* rpt = &report_buf[1];
    hid_keys_t key_state[2];
    hid_keys_t key_delta;
    memset(&key_state[0], 0, sizeof(hid_keys_t));
    memset(&key_state[1], 0, sizeof(hid_keys_t));
    int cur_idx = 0;
    int prev_idx = 1;
    int modifiers = 0;

    for (;;) {
        mxio_wait_fd(listener->fd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
        int r = read(listener->fd, report_buf, sizeof(report_buf));
        if (r < 0) {
            break; // will be restarted by poll thread if needed
        }
        if ((size_t)(r) != sizeof(report_buf)) {
            continue;
        }
        // verify this is report 0
        if (report_buf[0] != 0) continue;
        // eat the input if there is no active vc
        if (!active_vc) continue;
        // process the key
        int consumed = 0;
        uint8_t keycode;
        hid_kbd_parse_report(rpt, &key_state[cur_idx]);

        hid_kbd_pressed_keys(&key_state[prev_idx], &key_state[cur_idx], &key_delta);
        hid_for_every_key(&key_delta, keycode) {
            switch (keycode) {
            // modifier keys are special
            case HID_USAGE_KEY_LEFT_SHIFT:
                modifiers |= MOD_LSHIFT;
                break;
            case HID_USAGE_KEY_RIGHT_SHIFT:
                modifiers |= MOD_RSHIFT;
                break;
            case HID_USAGE_KEY_LEFT_ALT:
                modifiers |= MOD_LALT;
                break;
            case HID_USAGE_KEY_RIGHT_ALT:
                modifiers |= MOD_RALT;
                break;
            case HID_USAGE_KEY_LEFT_CTRL:
                modifiers |= MOD_LCTRL;
                break;
            case HID_USAGE_KEY_RIGHT_CTRL:
                modifiers |= MOD_RCTRL;
                break;

            case HID_USAGE_KEY_F1:
                vc_set_active_console(active_vc_index == 0 ? vc_count - 1 : active_vc_index - 1);
                consumed = 1;
                break;
            case HID_USAGE_KEY_F2:
                vc_set_active_console(active_vc_index == vc_count - 1 ? 0 : active_vc_index + 1);
                consumed = 1;
                break;

            case HID_USAGE_KEY_UP:
                if (modifiers & MOD_LALT || modifiers & MOD_RALT) {
                    vc_device_scroll_viewport(active_vc, -1);
                    consumed = 1;
                }
                break;
            case HID_USAGE_KEY_DOWN:
                if (modifiers & MOD_LALT || modifiers & MOD_RALT) {
                    vc_device_scroll_viewport(active_vc, 1);
                    consumed = 1;
                }
                break;
            case HID_USAGE_KEY_PAGEUP:
                if (modifiers & MOD_LSHIFT || modifiers & MOD_RSHIFT) {
                    vc_device_scroll_viewport(active_vc, -(active_vc->rows / 2));
                    consumed = 1;
                }
                break;
            case HID_USAGE_KEY_PAGEDOWN:
                if (modifiers & MOD_LSHIFT || modifiers & MOD_RSHIFT) {
                    vc_device_scroll_viewport(active_vc, active_vc->rows / 2);
                    consumed = 1;
                }
                break;

            // eat everything else
            default:; // nothing
            }
        }

        hid_kbd_released_keys(&key_state[prev_idx], &key_state[cur_idx], &key_delta);
        hid_for_every_key(&key_delta, keycode) {
            switch (keycode) {
            // modifier keys are special
            case HID_USAGE_KEY_LEFT_SHIFT:
                modifiers &= ~MOD_LSHIFT;
                break;
            case HID_USAGE_KEY_RIGHT_SHIFT:
                modifiers &= ~MOD_RSHIFT;
                break;
            case HID_USAGE_KEY_LEFT_ALT:
                modifiers &= ~MOD_LALT;
                break;
            case HID_USAGE_KEY_RIGHT_ALT:
                modifiers &= ~MOD_RALT;
                break;
            case HID_USAGE_KEY_LEFT_CTRL:
                modifiers &= ~MOD_LCTRL;
                break;
            case HID_USAGE_KEY_RIGHT_CTRL:
                modifiers &= ~MOD_RCTRL;
                break;

            default:; // nothing
            }
        }

        if (!consumed) {
            // TODO: decouple char device from actual device
            // TODO: ensure active vc can't change while this is going on
            mxr_mutex_lock(&active_vc->fifo.lock);
            if ((mx_hid_fifo_size(&active_vc->fifo) == 0) && (active_vc->charcount == 0)) {
                active_vc->flags |= VC_FLAG_RESETSCROLL;
                device_state_set(&active_vc->device, DEV_STATE_READABLE);
            }
            mx_hid_fifo_write(&active_vc->fifo, rpt, sizeof(report_buf - 1));
            mxr_mutex_unlock(&active_vc->fifo.lock);
        }

        // swap key states
        cur_idx = 1 - cur_idx;
        prev_idx = 1 - prev_idx;
    }
    close(listener->fd);
    listener->fd = -1;
    listener->flags &= ~INPUT_LISTENER_FLAG_RUNNING;
    // keep this in the list so we don't have to alloc again when a new device appears
    return 0;
}

#define DEV_INPUT "/dev/class/input"

static int vc_input_devices_poll_thread(void* arg) {
    for (;;) {
        struct dirent* de;
        DIR* dir = opendir(DEV_INPUT);
        if (!dir) {
            xprintf("vc: error opening %s\n", DEV_INPUT);
            return ERR_INTERNAL;
        }
        char dname[128];
        char tname[128];
        mx_status_t status;
        while ((de = readdir(dir)) != NULL) {
            snprintf(dname, sizeof(dname), "%s/%s", DEV_INPUT, de->d_name);

            // is there already a listener for this device?
            bool found = false;
            input_listener_t* listener = NULL;
            list_for_every_entry (&input_listeners_list, listener, input_listener_t, node) {
                if (listener->flags & INPUT_LISTENER_FLAG_RUNNING && !strcmp(listener->dev_name, dname)) {
                    found = true;
                    break;
                }
            }
            // do nothing if a listener is already running for this device
            if (found) continue;
            // otherwise start a listener for it
            input_listener_t* free = NULL;
            list_for_every_entry (&input_listeners_list, listener, input_listener_t, node) {
                if (!(listener->flags & INPUT_LISTENER_FLAG_RUNNING)) {
                    free = listener;
                    break;
                }
            }
            if (!free) {
                free = calloc(1, sizeof(input_listener_t));
                if (!free) {
                    // wait for next loop
                    break;
                }
                list_add_tail(&input_listeners_list, &free->node);
            }
            // mark this as running
            free->flags |= INPUT_LISTENER_FLAG_RUNNING;
            // open the device
            strncpy(free->dev_name, dname, sizeof(free->dev_name));
            free->fd = open(free->dev_name, O_RDONLY);
            if (free->fd < 0) {
                free->flags &= ~INPUT_LISTENER_FLAG_RUNNING;
                continue;
            }
            // test to see if this is a device we can read
            int proto = INPUT_PROTO_NONE;
            int rc = mxio_ioctl(free->fd, INPUT_IOCTL_GET_PROTOCOL, NULL, 0, &proto, sizeof(proto));
            if (rc > 0 && proto != INPUT_PROTO_KBD) {
                // skip devices that aren't keyboards
                free->flags &= ~INPUT_LISTENER_FLAG_RUNNING;
                continue;
            }
            // start a thread to wait on the fd
            snprintf(tname, sizeof(tname), "vc-input-%s", de->d_name);
            status = mxr_thread_create(vc_input_thread, (void*)free, tname, &free->t);
            if (status < 0) {
                xprintf("vc: input thread %s did not start (status=%d)\n", tname, status);
                free->flags &= ~INPUT_LISTENER_FLAG_RUNNING;
            }
        }
        closedir(dir);
        usleep(1000 * 1000); // 1 second
        //TODO: wait on directory changed (swetland)
    }
    return NO_ERROR;
}

static void __vc_set_active(vc_device_t* dev, unsigned index) {
    // must be called while holding vc_lock
    if (active_vc)
        active_vc->active = false;
    dev->active = true;
    active_vc = dev;
    active_vc->flags &= ~VC_FLAG_HASINPUT;
    active_vc_index = index;
}

mx_status_t vc_set_active_console(unsigned console) {
    if (console >= vc_count)
        return ERR_INVALID_ARGS;

    unsigned i = 0;
    vc_device_t* device = NULL;
    mxr_mutex_lock(&vc_lock);
    list_for_every_entry (&vc_list, device, vc_device_t, node) {
        if (i == console)
            break;
        i++;
    }
    if (device == active_vc) {
        mxr_mutex_unlock(&vc_lock);
        return NO_ERROR;
    }
    __vc_set_active(device, console);
    mxr_mutex_unlock(&vc_lock);
    vc_device_render(active_vc);
    return NO_ERROR;
}

void vc_get_status_line(char* str, int n) {
    vc_device_t* device = NULL;
    char* ptr = str;
    unsigned i = 0;
    // TODO add process name, etc.
    mxr_mutex_lock(&vc_lock);
    list_for_every_entry (&vc_list, device, vc_device_t, node) {
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
    mxr_mutex_unlock(&vc_lock);
}

// implement device protocol:

static mx_status_t vc_device_release(mx_device_t* dev) {
    vc_device_t* vc = get_vc_device(dev);

    mxr_mutex_lock(&vc_lock);
    list_delete(&vc->node);
    vc_count -= 1;

    if (vc->active) active_vc = NULL;

    // need to fixup active_vc and active_vc_index after deletion
    vc_device_t* d = NULL;
    unsigned i = 0;
    list_for_every_entry(&vc_list, d, vc_device_t, node) {
        if (active_vc) {
            if (d == active_vc) {
                active_vc_index = i;
                break;
            }
        } else {
            if (i == active_vc_index) {
                __vc_set_active(d, i);
                break;
            }
        }
        i++;
    }
    mxr_mutex_unlock(&vc_lock);

    vc_device_free(vc);

    // redraw the status line, or the full screen
    if (active_vc) vc_device_render(active_vc);
    return NO_ERROR;
}

static ssize_t vc_device_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    vc_device_t* vc = get_vc_device(dev);

    uint8_t report[8];
    hid_keys_t key_delta;
    ssize_t r = 0;
    mxr_mutex_lock(&vc->fifo.lock);
    int cur_idx = vc->key_idx;
    int prev_idx = 1 - cur_idx;
    while (count > 0) {
        if (vc->charcount > 0) {
            if (count > vc->charcount) {
                count = vc->charcount;
            }
            memcpy(buf, vc->chardata, count);
            vc->charcount -= count;
            if (vc->charcount > 0) {
                memmove(vc->chardata, vc->chardata + count, vc->charcount);
            }
            r = count;
            break;
        }
        if (mx_hid_fifo_read(&vc->fifo, report, sizeof(report)) < (ssize_t)sizeof(report)) {
            // TODO: better error?
            r = 0;
            break;
        }

        uint8_t keycode;
        char* str = vc->chardata;
        hid_kbd_parse_report(report, &vc->key_states[cur_idx]);

        hid_kbd_pressed_keys(&vc->key_states[prev_idx], &vc->key_states[cur_idx], &key_delta);
        hid_for_every_key(&key_delta, keycode) {
            uint8_t ch = hid_map_key(keycode, vc->modifiers & MOD_SHIFT, vc->keymap);
            if (ch) {
                if (vc->modifiers & MOD_CTRL) {
                    uint8_t sub = vc->modifiers & MOD_SHIFT ? 'A' : 'a';
                    str[0] = ch - sub + 1;
                } else {
                    str[0] = ch;
                }
                vc->charcount = 1;
                continue;
            }

            switch (keycode) {
            case HID_USAGE_KEY_LEFT_SHIFT:
                vc->modifiers |= MOD_LSHIFT;
                break;
            case HID_USAGE_KEY_RIGHT_SHIFT:
                vc->modifiers |= MOD_RSHIFT;
                break;
            case HID_USAGE_KEY_LEFT_CTRL:
                vc->modifiers |= MOD_LCTRL;
                break;
            case HID_USAGE_KEY_RIGHT_CTRL:
                vc->modifiers |= MOD_RCTRL;
                break;
            case HID_USAGE_KEY_LEFT_ALT:
                vc->modifiers |= MOD_LALT;
                break;
            case HID_USAGE_KEY_RIGHT_ALT:
                vc->modifiers |= MOD_RALT;
                break;

            // generate special stuff for a few different keys
            case HID_USAGE_KEY_ENTER:
            case HID_USAGE_KEY_KP_ENTER:
                str[0] = '\n';
                vc->charcount = 1;
                break;
            case HID_USAGE_KEY_BACKSPACE:
                str[0] = '\b';
                vc->charcount = 1;
                break;
            case HID_USAGE_KEY_TAB:
                str[0] = '\t';
                vc->charcount = 1;
                break;
            case HID_USAGE_KEY_ESC:
                str[0] = 0x1b;
                vc->charcount = 1;
                break;

            // generate vt100 key codes for arrows
            case HID_USAGE_KEY_UP:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 65;
                vc->charcount = 3;
                break;
            case HID_USAGE_KEY_DOWN:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 66;
                vc->charcount = 3;
                break;
            case HID_USAGE_KEY_RIGHT:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 67;
                vc->charcount = 3;
                break;
            case HID_USAGE_KEY_LEFT:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 68;
                vc->charcount = 3;
                break;
            case HID_USAGE_KEY_HOME:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 'H';
                vc->charcount = 3;
                break;
            case HID_USAGE_KEY_END:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = 'F';
                vc->charcount = 3;
                break;
            case HID_USAGE_KEY_PAGEUP:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = '5';
                str[3] = '~';
                vc->charcount = 4;
                break;
            case HID_USAGE_KEY_PAGEDOWN:
                str[0] = 0x1b;
                str[1] = '[';
                str[2] = '6';
                str[3] = '~';
                vc->charcount = 4;
                break;

            default:
                // ignore unknown keys; character keys were handled above
                break;
            }
        }

        hid_kbd_released_keys(&vc->key_states[prev_idx], &vc->key_states[cur_idx], &key_delta);
        hid_for_every_key(&key_delta, keycode) {
            switch (keycode) {
            case HID_USAGE_KEY_LEFT_SHIFT:
                vc->modifiers &= (~MOD_LSHIFT);
                break;
            case HID_USAGE_KEY_RIGHT_SHIFT:
                vc->modifiers &= (~MOD_RSHIFT);
                break;
            case HID_USAGE_KEY_LEFT_CTRL:
                vc->modifiers &= (~MOD_LCTRL);
                break;
            case HID_USAGE_KEY_RIGHT_CTRL:
                vc->modifiers &= (~MOD_RCTRL);
                break;
            case HID_USAGE_KEY_LEFT_ALT:
                vc->modifiers &= (~MOD_LALT);
                break;
            case HID_USAGE_KEY_RIGHT_ALT:
                vc->modifiers &= (~MOD_RALT);
                break;
            }
        }

        // swap key states
        cur_idx = 1 - cur_idx;
        prev_idx = 1 - prev_idx;
    }
    if ((mx_hid_fifo_size(&vc->fifo) == 0) && (vc->charcount == 0)) {
        device_state_clr(dev, DEV_STATE_READABLE);
    }
    vc->key_idx = cur_idx;
    mxr_mutex_unlock(&vc->fifo.lock);
    return r;
}

static ssize_t vc_device_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    vc_device_t* vc = get_vc_device(dev);
    mxr_mutex_lock(&vc->lock);
    vc->invy0 = vc->rows + 1;
    vc->invy1 = -1;
    const uint8_t* str = (const uint8_t*)buf;
    for (size_t i = 0; i < count; i++) {
        vc->textcon.putc(&vc->textcon, str[i]);
    }
    if (vc->invy1 >= 0) {
        vc_gfx_invalidate(vc, 0, vc->invy0, vc->columns, vc->invy1 - vc->invy0);
    }
    if (!vc->active && !(vc->flags & VC_FLAG_HASINPUT)) {
        vc->flags |= VC_FLAG_HASINPUT;
        vc_device_write_status(vc);
        vc_gfx_invalidate_status(vc);
    }
    mxr_mutex_unlock(&vc->lock);
    return count;
}

static ssize_t vc_device_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    vc_device_t* vc = get_vc_device(dev);
    switch (op) {
    case CONSOLE_OP_GET_DIMENSIONS: {
        ioctl_console_dimensions_t* dims = reply;
        if (max < sizeof(*dims)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        dims->width = vc->columns;
        dims->height = vc->rows;
        return sizeof(*dims);
    }
    case DISPLAY_OP_GET_FB: {
        if (max < sizeof(ioctl_display_get_fb_t)) {
            return ERR_NOT_ENOUGH_BUFFER;
        }
        ioctl_display_get_fb_t* fb = reply;
        fb->info.format = vc->gfx->format;
        fb->info.width = vc->gfx->width;
        fb->info.height = vc->gfx->height;
        fb->info.stride = vc->gfx->stride;
        fb->info.pixelsize = vc->gfx->pixelsize;
        fb->info.flags = 0;
        //TODO: take away access to the vmo when the client closes the device
        fb->vmo = mx_handle_duplicate(vc->gfx_vmo, MX_RIGHT_SAME_RIGHTS);
        return sizeof(ioctl_display_get_fb_t);
    }
    case DISPLAY_OP_FLUSH_FB:
        vc_gfx_invalidate_all(vc);
        return NO_ERROR;
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_protocol_device_t vc_device_proto = {
    .release = vc_device_release,
    .read = vc_device_read,
    .write = vc_device_write,
    .ioctl = vc_device_ioctl,
};

extern mx_driver_t _driver_vc_root;

// opening the root device returns a new vc device instance
static mx_status_t vc_root_open(mx_device_t* dev, mx_device_t** dev_out, uint32_t flags) {
    mx_status_t status;
    vc_device_t* device;
    if ((status = vc_device_alloc(&hw_gfx, &device)) < 0) {
        return status;
    }

    // init the new device
    char name[8];
    snprintf(name, sizeof(name), "vc%u", vc_count);
    status = device_init(&device->device, &_driver_vc_root, name, &vc_device_proto);
    if (status != NO_ERROR) {
        return status;
    }

    if (dev) {
        // if called normally, add the instance
        // if dev is null, we're creating the log console
        device->device.protocol_id = MX_PROTOCOL_CONSOLE;
        status = device_add_instance(&device->device, dev);
        if (status != NO_ERROR) {
            vc_device_free(device);
            return status;
        }
    }

    // add to the vc list
    mxr_mutex_lock(&vc_lock);
    list_add_tail(&vc_list, &device->node);
    vc_count++;
    mxr_mutex_unlock(&vc_lock);

    // make this the active vc if it's the first one
    if (!active_vc) {
        vc_set_active_console(0);
    } else {
        vc_device_render(active_vc);
    }

    *dev_out = &device->device;
    return NO_ERROR;
}

static int vc_log_reader_thread(void* arg) {
    mx_device_t* dev = arg;
    mx_handle_t h;

    if ((h = mx_log_create(MX_LOG_FLAG_READABLE)) < 0) {
        printf("vc log listener: cannot open log\n");
        return -1;
    }

    char buf[MX_LOG_RECORD_MAX];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    while (mx_log_read(h, MX_LOG_RECORD_MAX, rec, MX_LOG_FLAG_WAIT) > 0) {
        char tmp[64];
        snprintf(tmp, 64, "[%05d.%03d] %c ",
                 (int)(rec->timestamp / 1000000000ULL),
                 (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                 (rec->flags & MX_LOG_FLAG_KERNEL) ? 'K' : 'U');
        vc_device_write(dev, tmp, strlen(tmp), 0);
        vc_device_write(dev, rec->data, rec->datalen, 0);
        if ((rec->datalen == 0) || (rec->data[rec->datalen - 1] != '\n')) {
            vc_device_write(dev, "\n", 1, 0);
        }
    }
    return 0;
}

static mx_protocol_device_t vc_root_proto = {
    .open = vc_root_open,
};

static mx_status_t vc_root_bind(mx_driver_t* drv, mx_device_t* dev) {
    if (vc_initialized) {
        // disallow multiple instances
        return ERR_NOT_SUPPORTED;
    }

    mx_display_protocol_t* disp;
    mx_status_t status;
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

    // publish the root vc device. opening this device will create a new vc
    mx_device_t* device;
    status = device_create(&device, drv, VC_DEVNAME, &vc_root_proto);
    if (status != NO_ERROR) {
        return status;
    }

    // start a thread to listen for new input devices
    status = mxr_thread_create(vc_input_devices_poll_thread, NULL, "vc-inputdev-poll", &input_poll_thread);
    if (status != NO_ERROR) {
        xprintf("vc: input polling thread did not start (status=%d)\n", status);
    }

    device->protocol_id = MX_PROTOCOL_CONSOLE;
    status = device_add(device, dev);
    if (status != NO_ERROR) {
        goto fail;
    }

    vc_initialized = true;
    xprintf("initialized vc on display %s, width=%u height=%u stride=%u format=%u\n", dev->name, info.width, info.height, info.stride, format);

    if (vc_root_open(NULL, &dev, 0) == NO_ERROR) {
        mxr_thread_t* t;
        mxr_thread_create(vc_log_reader_thread, dev, "vc-log-reader", &t);
    }

    return NO_ERROR;
fail:
    free(device);
    // TODO clean up threads
    return status;
}

static mx_bind_inst_t binding[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_DISPLAY),
};

mx_driver_t _driver_vc_root BUILTIN_DRIVER = {
    .name = "vc-root",
    .ops = {
        .bind = vc_root_bind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
