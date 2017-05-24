// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/input.h>

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <gfx/gfx.h>
#include <hid/usages.h>
#include <magenta/device/console.h>
#include <magenta/listnode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <mxio/io.h>
#include <mxio/watcher.h>
#include <mxtl/auto_lock.h>

#include <magenta/atomic.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <magenta/syscalls/object.h>

#define VCDEBUG 1

#include "keyboard-vt100.h"
#include "keyboard.h"
#include "vc.h"
#include "vcdebug.h"

#define VC_DEVNAME "vc"

// framebuffer
static gfx_surface g_hw_gfx;
static mx_device_t* g_fb_device;
static mx_device_t* g_root_device;
static mx_display_protocol_t* g_fb_display_protocol;

static thrd_t g_input_poll_thread;

// single driver instance
static bool g_vc_initialized = false;

// remember whether the virtual console controls the display
static int g_vc_owns_display = 1;

static mx_handle_t g_vc_owner_event = MX_HANDLE_INVALID;

mtx_t g_vc_lock = MTX_INIT;

static struct list_node g_vc_list TA_GUARDED(g_vc_lock)
    = LIST_INITIAL_VALUE(g_vc_list);
static unsigned g_vc_count TA_GUARDED(g_vc_lock) = 0;
static vc_device_t* g_active_vc TA_GUARDED(g_vc_lock);
static unsigned g_active_vc_index TA_GUARDED(g_vc_lock);
static vc_battery_info_t g_battery_info TA_GUARDED(g_vc_lock);

static mx_status_t vc_set_active_console(unsigned console) TA_REQ(g_vc_lock);

static void vc_device_toggle_framebuffer() {
    if (g_fb_display_protocol->acquire_or_release_display)
        g_fb_display_protocol->acquire_or_release_display(g_fb_device);
}

static void vc_display_ownership_callback(bool acquired) {
    atomic_store(&g_vc_owns_display, acquired ? 1 : 0);
    if (acquired) {
        mx_object_signal(g_vc_owner_event, MX_USER_SIGNAL_1, MX_USER_SIGNAL_0);
    } else {
        mx_object_signal(g_vc_owner_event, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1);
    }
}

// Process key sequences that affect the console (scrolling, switching
// console, etc.) without sending input to the current console.  This
// returns whether this key press was handled.
static bool vc_handle_control_keys(uint8_t keycode,
                                   int modifiers) TA_REQ(g_vc_lock) {
    switch (keycode) {
    case HID_USAGE_KEY_F1 ... HID_USAGE_KEY_F10:
        if (modifiers & MOD_ALT) {
            vc_set_active_console(keycode - HID_USAGE_KEY_F1);
            return true;
        }
        break;

    case HID_USAGE_KEY_F11:
        if (g_active_vc && (modifiers & MOD_ALT)) {
            vc_device_set_fullscreen(g_active_vc, !(g_active_vc->flags & VC_FLAG_FULLSCREEN));
            return true;
        }
        break;

    case HID_USAGE_KEY_TAB:
        if (modifiers & MOD_ALT) {
            if (modifiers & MOD_SHIFT) {
                vc_set_active_console(g_active_vc_index == 0 ? g_vc_count - 1 : g_active_vc_index - 1);
            } else {
                vc_set_active_console(g_active_vc_index == g_vc_count - 1 ? 0 : g_active_vc_index + 1);
            }
            return true;
        }
        break;

    case HID_USAGE_KEY_UP:
        if (modifiers & MOD_ALT) {
            vc_device_scroll_viewport(g_active_vc, -1);
            return true;
        }
        break;
    case HID_USAGE_KEY_DOWN:
        if (modifiers & MOD_ALT) {
            vc_device_scroll_viewport(g_active_vc, 1);
            return true;
        }
        break;
    case HID_USAGE_KEY_PAGEUP:
        if (modifiers & MOD_SHIFT) {
            vc_device_scroll_viewport(g_active_vc, -(vc_device_rows(g_active_vc) / 2));
            return true;
        }
        break;
    case HID_USAGE_KEY_PAGEDOWN:
        if (modifiers & MOD_SHIFT) {
            vc_device_scroll_viewport(g_active_vc, vc_device_rows(g_active_vc) / 2);
            return true;
        }
        break;
    case HID_USAGE_KEY_HOME:
        if (modifiers & MOD_SHIFT) {
            vc_device_scroll_viewport_top(g_active_vc);
            return true;
        }
        break;
    case HID_USAGE_KEY_END:
        if (modifiers & MOD_SHIFT) {
            vc_device_scroll_viewport_bottom(g_active_vc);
            return true;
        }
        break;
    }
    return false;
}

// Process key sequences that affect the low-level control of the system
// (switching display ownership, rebooting).  This returns whether this key press
// was handled.
static bool vc_handle_device_control_keys(uint8_t keycode,
                                          int modifiers) TA_REQ(g_vc_lock) {
    switch (keycode) {
    case HID_USAGE_KEY_DELETE:
        // Provide a CTRL-ALT-DEL reboot sequence
        if ((modifiers & MOD_CTRL) && (modifiers & MOD_ALT)) {
            int fd;
            // Send the reboot command to devmgr
            if ((fd = open("/dev/misc/dmctl", O_WRONLY)) >= 0) {
                write(fd, "reboot", strlen("reboot"));
                close(fd);
            }
            return true;
        }
        break;

    case HID_USAGE_KEY_ESC:
        if (modifiers & MOD_ALT) {
            vc_device_toggle_framebuffer();
            return true;
        }
        break;
    }
    return false;
}

static void vc_handle_key_press(uint8_t keycode, int modifiers) {
    mxtl::AutoLock lock(&g_vc_lock);

    // Handle device-level control keys
    if (vc_handle_device_control_keys(keycode, modifiers))
        return;

    // Handle other keys only if we own the display
    if (atomic_load(&g_vc_owns_display) == 0)
        return;

    // Handle other control keys
    if (vc_handle_control_keys(keycode, modifiers))
        return;

    vc_device_t* dev = g_active_vc;
    char output[4];
    uint32_t length = hid_key_to_vt100_code(
        keycode, modifiers, dev->keymap, output, sizeof(output));
    if (length > 0) {
        // This writes multi-byte sequences atomically, so that if space
        // isn't available for the full sequence -- if the program running
        // on the console is currently not reading input -- then nothing is
        // written.  This has the nice property that we won't get partial
        // key code sequences in that case.
        mx_hid_fifo_write(&dev->fifo, output, length);

        if (dev->mxdev) {
            device_state_set(dev->mxdev, DEV_STATE_READABLE);
        }
        vc_device_scroll_viewport_bottom(dev);
    }
}

static int vc_watch_for_keyboard_devices_thread(void* arg) {
    vc_watch_for_keyboard_devices(vc_handle_key_press);
    return -1;
}

static void __vc_set_active(vc_device_t* dev, unsigned index) TA_REQ(g_vc_lock) {
    if (g_active_vc)
        g_active_vc->active = false;
    dev->active = true;
    g_active_vc = dev;
    g_active_vc->flags &= ~VC_FLAG_HASOUTPUT;
    g_active_vc_index = index;
}

static mx_status_t vc_set_console_to_active(vc_device_t* dev) TA_REQ(g_vc_lock) {
    if (dev == NULL)
        return ERR_INVALID_ARGS;

    unsigned i = 0;
    vc_device_t* device = NULL;

    list_for_every_entry (&g_vc_list, device, vc_device_t, node) {
        if (device == dev)
            break;
        i++;
    }
    if (i == g_vc_count) {
        return ERR_INVALID_ARGS;
    }
    __vc_set_active(dev, i);
    vc_device_render(g_active_vc);
    return NO_ERROR;
}

static mx_status_t vc_set_active_console(unsigned console) {
    if (console >= g_vc_count)
        return ERR_INVALID_ARGS;

    unsigned i = 0;
    vc_device_t* device = NULL;
    list_for_every_entry (&g_vc_list, device, vc_device_t, node) {
        if (i == console)
            break;
        i++;
    }
    if (device == g_active_vc) {
        return NO_ERROR;
    }
    __vc_set_active(device, console);
    vc_device_render(g_active_vc);
    return NO_ERROR;
}

void vc_get_status_line(char* str, int n) {
    vc_device_t* device = NULL;
    char* ptr = str;
    unsigned i = 0;
    // TODO add process name, etc.
    list_for_every_entry (&g_vc_list, device, vc_device_t, node) {
        if (n <= 0) {
            break;
        }

        int lines = vc_device_get_scrollback_lines(device);
        int chars = snprintf(ptr, n, "%s[%u] %s%c    %c%c \033[m",
                             device->active ? "\033[33m\033[1m" : "",
                             i,
                             device->title,
                             device->flags & VC_FLAG_HASOUTPUT ? '*' : ' ',
                             lines > 0 && -device->viewport_y < lines ? '<' : ' ',
                             device->viewport_y < 0 ? '>' : ' ');
        ptr += chars;
        n -= chars;
        i++;
    }
}

void vc_get_battery_info(vc_battery_info_t* info) {
    memcpy(info, &g_battery_info, sizeof(vc_battery_info_t));
}

// implement device protocol:

static void vc_device_release(void* ctx) {
    vc_device_t* vc = static_cast<vc_device_t*>(ctx);

    mxtl::AutoLock lock(&g_vc_lock);

    list_delete(&vc->node);
    g_vc_count -= 1;

    if (vc->active) {
        g_active_vc = NULL;
        if (g_active_vc_index >= g_vc_count) {
            g_active_vc_index = g_vc_count - 1;
        }
    }

    // need to fixup g_active_vc and g_active_vc_index after deletion
    vc_device_t* d = NULL;
    unsigned i = 0;
    list_for_every_entry (&g_vc_list, d, vc_device_t, node) {
        if (g_active_vc) {
            if (d == g_active_vc) {
                g_active_vc_index = i;
                break;
            }
        } else {
            if (i == g_active_vc_index) {
                __vc_set_active(d, i);
                break;
            }
        }
        i++;
    }

    vc_device_free(vc);

    // redraw the status line, or the full screen
    if (g_active_vc) {
        vc_device_render(g_active_vc);
    }
}

static mx_status_t vc_device_read(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {
    vc_device_t* vc = static_cast<vc_device_t*>(ctx);

    mxtl::AutoLock lock(&g_vc_lock);

    ssize_t result = mx_hid_fifo_read(&vc->fifo, buf, count);
    if (mx_hid_fifo_size(&vc->fifo) == 0) {
        device_state_clr(vc->mxdev, DEV_STATE_READABLE);
    }

    if (result == 0) {
        result = ERR_SHOULD_WAIT;
    } else {
        *actual = result;
        result = NO_ERROR;
    }
    return (mx_status_t)result;
}

static mx_status_t vc_device_op_write(void* ctx, const void* buf, size_t count, mx_off_t off,
                                      size_t* actual) {
    vc_device_t* vc = static_cast<vc_device_t*>(ctx);
    ssize_t result = vc_device_write(vc, buf, count, off);
    if (result >= 0) {
        *actual = result;
        result = NO_ERROR;
    }
    return (mx_status_t)result;
}

ssize_t vc_device_write(vc_device_t* vc, const void* buf, size_t count, mx_off_t off) {
    mxtl::AutoLock lock(&g_vc_lock);

    vc->invy0 = vc_device_rows(vc) + 1;
    vc->invy1 = -1;
    const uint8_t* str = (const uint8_t*)buf;
    for (size_t i = 0; i < count; i++) {
        vc->textcon.putc(&vc->textcon, str[i]);
    }
    if (vc->invy1 >= 0) {
        int rows = vc_device_rows(vc);
        // Adjust for the current viewport position.  Convert
        // console-relative row numbers to screen-relative row numbers.
        int invalidate_y0 = MIN(vc->invy0 - vc->viewport_y, rows);
        int invalidate_y1 = MIN(vc->invy1 - vc->viewport_y, rows);
        vc_gfx_invalidate(vc, 0, invalidate_y0,
                          vc->columns, invalidate_y1 - invalidate_y0);
    }
    if (!vc->active && !(vc->flags & VC_FLAG_HASOUTPUT)) {
        vc->flags |= VC_FLAG_HASOUTPUT;
        vc_device_write_status(vc);
        vc_gfx_invalidate_status(vc);
    }
    return count;
}

static mx_status_t vc_device_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                                   void* reply, size_t max, size_t* out_actual) {
    vc_device_t* vc = static_cast<vc_device_t*>(ctx);

    mxtl::AutoLock lock(&g_vc_lock);

    switch (op) {
    case IOCTL_CONSOLE_GET_DIMENSIONS: {
        auto* dims = reinterpret_cast<ioctl_console_dimensions_t*>(reply);
        if (max < sizeof(*dims)) {
            return ERR_BUFFER_TOO_SMALL;
        }
        dims->width = vc->columns;
        dims->height = vc_device_rows(vc);
        *out_actual = sizeof(*dims);
        return NO_ERROR;
    }
    case IOCTL_CONSOLE_SET_ACTIVE_VC:
        return vc_set_console_to_active(vc);
    case IOCTL_DISPLAY_GET_FB: {
        if (max < sizeof(ioctl_display_get_fb_t)) {
            return ERR_BUFFER_TOO_SMALL;
        }
        auto* fb = reinterpret_cast<ioctl_display_get_fb_t*>(reply);
        fb->info.format = vc->gfx->format;
        fb->info.width = vc->gfx->width;
        fb->info.height = vc->gfx->height;
        fb->info.stride = vc->gfx->stride;
        fb->info.pixelsize = vc->gfx->pixelsize;
        fb->info.flags = 0;
        //TODO: take away access to the vmo when the client closes the device
        mx_status_t status = mx_handle_duplicate(vc->gfx_vmo, MX_RIGHT_SAME_RIGHTS, &fb->vmo);
        if (status < 0) {
            return status;
        } else {
            *out_actual = sizeof(ioctl_display_get_fb_t);
            return NO_ERROR;
        }
    }
    case IOCTL_DISPLAY_FLUSH_FB:
        vc_gfx_invalidate_all(vc);
        return NO_ERROR;
    case IOCTL_DISPLAY_FLUSH_FB_REGION: {
        auto* rect = reinterpret_cast<const ioctl_display_region_t*>(cmd);
        if (cmdlen < sizeof(*rect)) {
            return ERR_INVALID_ARGS;
        }
        vc_gfx_invalidate_region(vc, rect->x, rect->y, rect->width, rect->height);
        return NO_ERROR;
    }
    case IOCTL_DISPLAY_SET_FULLSCREEN: {
        if (cmdlen < sizeof(uint32_t) || !cmd) {
            return ERR_INVALID_ARGS;
        }
        vc_device_set_fullscreen(vc, !!*(uint32_t*)cmd);
        return NO_ERROR;
    }
    case IOCTL_DISPLAY_GET_OWNERSHIP_CHANGE_EVENT: {
        if (max < sizeof(mx_handle_t)) {
            return ERR_BUFFER_TOO_SMALL;
        }
        auto* evt = reinterpret_cast<mx_handle_t*>(reply);
        mx_rights_t client_rights = MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ;
        mx_status_t status = mx_handle_duplicate(g_vc_owner_event, client_rights, evt);
        if (status < 0) {
            return status;
        } else {
            *out_actual = sizeof(mx_handle_t);
            return NO_ERROR;
        }
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_protocol_device_t vc_device_proto;

// Create a new vc_device_t and add it to the console list.
static mx_status_t vc_device_create(vc_device_t** vc_out) {
    mxtl::AutoLock lock(&g_vc_lock);

    mx_status_t status;
    vc_device_t* device;
    if ((status = vc_device_alloc(&g_hw_gfx, &device)) < 0) {
        return status;
    }

    // add to the vc list
    list_add_tail(&g_vc_list, &device->node);
    g_vc_count++;

    // make this the active vc if it's the first one
    if (!g_active_vc) {
        vc_set_active_console(0);
    } else {
        vc_device_render(g_active_vc);
    }

    *vc_out = device;
    return NO_ERROR;
}

static mx_status_t vc_root_open(void* ctx, mx_device_t** dev_out, uint32_t flags) {
    vc_device_t* vc;
    mx_status_t status = vc_device_create(&vc);
    if (status != NO_ERROR) {
        return status;
    }

    mxtl::AutoLock lock(&g_vc_lock);

    // Create an mx_device_t for the vc_device_t.
    char name[8];
    snprintf(name, sizeof(name), "vc%u", g_vc_count);

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = name;
    args.ctx = vc;
    args.ops = &vc_device_proto;
    args.proto_id = MX_PROTOCOL_CONSOLE;
    args.flags = DEVICE_ADD_INSTANCE;

    status = device_add(g_root_device, &args, &vc->mxdev);
    if (status != NO_ERROR) {
        vc_device_free(vc);
        return status;
    }

    *dev_out = vc->mxdev;
    return NO_ERROR;
}

static int vc_log_reader_thread(void* arg) {
    auto dev = reinterpret_cast<vc_device_t*>(arg);
    mx_handle_t h;

    if (mx_log_create(MX_LOG_FLAG_READABLE, &h) < 0) {
        printf("vc log listener: cannot open log\n");
        return -1;
    }

    char buf[MX_LOG_RECORD_MAX];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    mx_status_t status;
    for (;;) {
        if ((status = mx_log_read(h, MX_LOG_RECORD_MAX, rec, 0)) < 0) {
            if (status == ERR_SHOULD_WAIT) {
                mx_object_wait_one(h, MX_LOG_READABLE, MX_TIME_INFINITE, NULL);
                continue;
            }
            break;
        }
        char tmp[64];
        snprintf(tmp, 64, "\033[32m%05d.%03d\033[39m] \033[31m%05" PRIu64 ".\033[36m%05" PRIu64 "\033[39m> ",
                 (int)(rec->timestamp / 1000000000ULL),
                 (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                 rec->pid, rec->tid);
        vc_device_write(dev, tmp, strlen(tmp), 0);
        vc_device_write(dev, rec->data, rec->datalen, 0);
        if ((rec->datalen == 0) || (rec->data[rec->datalen - 1] != '\n')) {
            vc_device_write(dev, "\n", 1, 0);
        }
    }

    const char* oops = "<<LOG ERROR>>\n";
    vc_device_write(dev, oops, strlen(oops), 0);

    return 0;
}

static int vc_battery_poll_thread(void* arg) {
    int battery_fd = static_cast<int>(reinterpret_cast<uintptr_t>(arg));
    char str[16];
    for (;;) {
        ssize_t length = read(battery_fd, str, sizeof(str) - 1);
        {
            mxtl::AutoLock lock(&g_vc_lock);
            if (length < 1 || str[0] == 'e') {
                g_battery_info.state = ERROR;
                g_battery_info.pct = -1;
            } else {
                str[length] = '\0';
                if (str[0] == 'c') {
                    g_battery_info.state = CHARGING;
                    g_battery_info.pct = atoi(&str[1]);
                } else {
                    g_battery_info.state = NOT_CHARGING;
                    g_battery_info.pct = atoi(str);
                }
            }
            if (g_active_vc) {
                vc_device_write_status(g_active_vc);
                vc_gfx_invalidate_status(g_active_vc);
            }
        }

        if (length <= 0) {
            printf("vc: read() on battery device returned %d\n",
                   static_cast<int>(length));
            break;
        }
        mx_nanosleep(mx_deadline_after(MX_MSEC(1000)));
    }
    close(battery_fd);
    return 0;
}

static mx_status_t vc_battery_device_added(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return NO_ERROR;
    }

    int battery_fd = openat(dirfd, fn, O_RDONLY);
    if (battery_fd < 0) {
        printf("vc: failed to open battery device \"%s\"\n", fn);
        return NO_ERROR;
    }

    printf("vc: found battery \"%s\"\n", fn);
    thrd_t t;
    int rc = thrd_create_with_name(
        &t, vc_battery_poll_thread,
        reinterpret_cast<void*>(static_cast<uintptr_t>(battery_fd)),
        "vc-battery-poll");
    if (rc != thrd_success) {
        close(battery_fd);
        return -1;
    }
    thrd_detach(t);
    return NO_ERROR;
}

static int vc_battery_dir_poll_thread(void* arg) {
    int dirfd;
    if ((dirfd = open("/dev/class/battery", O_DIRECTORY | O_RDONLY)) < 0) {
        return -1;
    }
    mxio_watch_directory(dirfd, vc_battery_device_added, NULL);
    close(dirfd);
    return 0;
}

static mx_protocol_device_t vc_root_proto;

static void display_flush(uint starty, uint endy) {
    g_fb_display_protocol->flush(g_fb_device);
}

static mx_status_t vc_root_bind(void* ctx, mx_device_t* dev, void** cookie) {
    if (g_vc_initialized) {
        // disallow multiple instances
        return ERR_NOT_SUPPORTED;
    }

    mx_display_protocol_t* disp;
    mx_status_t status;
    if ((status = device_op_get_protocol(dev, MX_PROTOCOL_DISPLAY, (void**)&disp)) < 0) {
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

    // initialize the hw surface
    if ((status = gfx_init_surface(&g_hw_gfx, framebuffer, info.width, info.height, info.stride, info.format, 0)) < 0) {
        return status;
    }

    // save some state
    g_fb_device = dev;
    g_fb_display_protocol = disp;

    // Create display event
    if ((status = mx_event_create(0, &g_vc_owner_event)) < 0) {
        return status;
    }

    // Request notification of display ownership changes
    if (disp->set_ownership_change_callback) {
        disp->set_ownership_change_callback(dev, &vc_display_ownership_callback);
    }

    // if the underlying device requires flushes, set the pointer to a flush op
    if (disp->flush) {
        g_hw_gfx.flush = display_flush;
    }

    // publish the root vc device. opening this device will create a new vc
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = VC_DEVNAME;
    args.ops = &vc_root_proto;
    args.proto_id = MX_PROTOCOL_CONSOLE;

    status = device_add(dev, &args, &g_root_device);
    if (status != NO_ERROR) {
        return status;
    }

    // start a thread to listen for new input devices
    int ret = thrd_create_with_name(
        &g_input_poll_thread, vc_watch_for_keyboard_devices_thread, NULL,
        "vc-inputdev-poll");
    if (ret != thrd_success) {
        xprintf("vc: input polling thread did not start (return value=%d)\n", ret);
    }

    g_vc_initialized = true;
    xprintf("initialized vc on display %s, width=%u height=%u stride=%u format=%u\n",
            device_get_name(dev), info.width, info.height, info.stride, info.format);

    vc_device_t* vc;
    if (vc_device_create(&vc) == NO_ERROR) {
        thrd_t t;
        thrd_create_with_name(&t, vc_log_reader_thread, vc, "vc-log-reader");
    }

    thrd_t u;
    thrd_create_with_name(&u, vc_battery_dir_poll_thread, NULL,
                          "vc-battery-dir-poll");

    return NO_ERROR;
}

static mx_driver_ops_t vc_root_driver_ops;

__attribute__((constructor)) static void initialize() {
    vc_device_proto.version = DEVICE_OPS_VERSION;
    vc_device_proto.release = vc_device_release;
    vc_device_proto.read = vc_device_read;
    vc_device_proto.write = vc_device_op_write;
    vc_device_proto.ioctl = vc_device_ioctl;

    vc_root_proto.version = DEVICE_OPS_VERSION;
    vc_root_proto.open = vc_root_open;

    vc_root_driver_ops.version = DRIVER_OPS_VERSION,
    vc_root_driver_ops.bind = vc_root_bind;
}

MAGENTA_DRIVER_BEGIN(vc_root, vc_root_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_DISPLAY),
MAGENTA_DRIVER_END(vc_root)
