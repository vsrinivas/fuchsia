// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <magenta/syscalls/object.h>

#define VCDEBUG 1

#include "keyboard-vt100.h"
#include "keyboard.h"
#include "vc.h"
#include "vcdebug.h"

thrd_t g_input_poll_thread;

// remember whether the virtual console controls the display
static int g_vc_owns_display = 1;

static mx_handle_t g_vc_owner_event = MX_HANDLE_INVALID;

mtx_t g_vc_lock = MTX_INIT;

static struct list_node g_vc_list TA_GUARDED(g_vc_lock)
    = LIST_INITIAL_VALUE(g_vc_list);
static unsigned g_vc_count TA_GUARDED(g_vc_lock) = 0;
static vc_t* g_active_vc TA_GUARDED(g_vc_lock);
static unsigned g_active_vc_index TA_GUARDED(g_vc_lock);
static vc_battery_info_t g_battery_info TA_GUARDED(g_vc_lock);

static mx_status_t vc_set_active_console(unsigned console) TA_REQ(g_vc_lock);

static void vc_toggle_framebuffer() {
    //TODO
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
            vc_set_fullscreen(g_active_vc, !(g_active_vc->flags & VC_FLAG_FULLSCREEN));
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
            vc_scroll_viewport(g_active_vc, -1);
            return true;
        }
        break;
    case HID_USAGE_KEY_DOWN:
        if (modifiers & MOD_ALT) {
            vc_scroll_viewport(g_active_vc, 1);
            return true;
        }
        break;
    case HID_USAGE_KEY_PAGEUP:
        if (modifiers & MOD_SHIFT) {
            vc_scroll_viewport(g_active_vc, -(vc_rows(g_active_vc) / 2));
            return true;
        }
        break;
    case HID_USAGE_KEY_PAGEDOWN:
        if (modifiers & MOD_SHIFT) {
            vc_scroll_viewport(g_active_vc, vc_rows(g_active_vc) / 2);
            return true;
        }
        break;
    case HID_USAGE_KEY_HOME:
        if (modifiers & MOD_SHIFT) {
            vc_scroll_viewport_top(g_active_vc);
            return true;
        }
        break;
    case HID_USAGE_KEY_END:
        if (modifiers & MOD_SHIFT) {
            vc_scroll_viewport_bottom(g_active_vc);
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
            vc_toggle_framebuffer();
            return true;
        }
        break;
    }
    return false;
}

static void vc_handle_key_press(uint8_t keycode, int modifiers) {
    mxtl::AutoLock lock(&g_vc_lock);

    // Handle vc-level control keys
    if (vc_handle_device_control_keys(keycode, modifiers))
        return;

    // Handle other keys only if we own the display
    if (atomic_load(&g_vc_owns_display) == 0)
        return;

    // Handle other control keys
    if (vc_handle_control_keys(keycode, modifiers))
        return;

    vc_t* vc = g_active_vc;
    char output[4];
    uint32_t length = hid_key_to_vt100_code(
        keycode, modifiers, vc->keymap, output, sizeof(output));
    if (length > 0) {
        //TODO: write(output,length) to vc
        vc_scroll_viewport_bottom(vc);
    }
}

static int vc_watch_for_keyboard_devices_thread(void* arg) {
    vc_watch_for_keyboard_devices(vc_handle_key_press);
    return -1;
}

static void __vc_set_active(vc_t* vc, unsigned index) TA_REQ(g_vc_lock) {
    if (g_active_vc)
        g_active_vc->active = false;
    vc->active = true;
    g_active_vc = vc;
    g_active_vc->flags &= ~VC_FLAG_HASOUTPUT;
    g_active_vc_index = index;
}

static mx_status_t vc_set_console_to_active(vc_t* to_vc) TA_REQ(g_vc_lock) {
    if (to_vc == NULL)
        return ERR_INVALID_ARGS;

    unsigned i = 0;
    vc_t* vc = NULL;

    list_for_every_entry (&g_vc_list, vc, vc_t, node) {
        if (to_vc == vc)
            break;
        i++;
    }
    if (i == g_vc_count) {
        return ERR_INVALID_ARGS;
    }
    __vc_set_active(to_vc, i);
    vc_render(g_active_vc);
    return NO_ERROR;
}

static mx_status_t vc_set_active_console(unsigned console) {
    if (console >= g_vc_count)
        return ERR_INVALID_ARGS;

    unsigned i = 0;
    vc_t* vc = NULL;
    list_for_every_entry (&g_vc_list, vc, vc_t, node) {
        if (i == console)
            break;
        i++;
    }
    if (vc == g_active_vc) {
        return NO_ERROR;
    }
    __vc_set_active(vc, console);
    vc_render(g_active_vc);
    return NO_ERROR;
}

void vc_get_status_line(char* str, int n) {
    vc_t* vc = NULL;
    char* ptr = str;
    unsigned i = 0;
    // TODO add process name, etc.
    list_for_every_entry (&g_vc_list, vc, vc_t, node) {
        if (n <= 0) {
            break;
        }

        int lines = vc_get_scrollback_lines(vc);
        int chars = snprintf(ptr, n, "%s[%u] %s%c    %c%c \033[m",
                             vc->active ? "\033[33m\033[1m" : "",
                             i,
                             vc->title,
                             vc->flags & VC_FLAG_HASOUTPUT ? '*' : ' ',
                             lines > 0 && -vc->viewport_y < lines ? '<' : ' ',
                             vc->viewport_y < 0 ? '>' : ' ');
        ptr += chars;
        n -= chars;
        i++;
    }
}

void vc_get_battery_info(vc_battery_info_t* info) {
    memcpy(info, &g_battery_info, sizeof(vc_battery_info_t));
}

static void vc_destroy(vc_t* vc) {
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
    vc_t* d = NULL;
    unsigned i = 0;
    list_for_every_entry (&g_vc_list, d, vc_t, node) {
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

    vc_free(vc);

    // redraw the status line, or the full screen
    if (g_active_vc) {
        vc_render(g_active_vc);
    }
}

//TODO wire output from vc proc to here
ssize_t vc_write(vc_t* vc, const void* buf, size_t count, mx_off_t off) {
    mxtl::AutoLock lock(&g_vc_lock);

    vc->invy0 = vc_rows(vc) + 1;
    vc->invy1 = -1;
    const uint8_t* str = (const uint8_t*)buf;
    for (size_t i = 0; i < count; i++) {
        vc->textcon.putc(&vc->textcon, str[i]);
    }
    if (vc->invy1 >= 0) {
        int rows = vc_rows(vc);
        // Adjust for the current viewport position.  Convert
        // console-relative row numbers to screen-relative row numbers.
        int invalidate_y0 = MIN(vc->invy0 - vc->viewport_y, rows);
        int invalidate_y1 = MIN(vc->invy1 - vc->viewport_y, rows);
        vc_gfx_invalidate(vc, 0, invalidate_y0,
                          vc->columns, invalidate_y1 - invalidate_y0);
    }
    if (!vc->active && !(vc->flags & VC_FLAG_HASOUTPUT)) {
        vc->flags |= VC_FLAG_HASOUTPUT;
        vc_write_status(vc);
        vc_gfx_invalidate_status(vc);
    }
    return count;
}

int g_fb_fd = -1;

// Create a new vc_t and add it to the console list.
static mx_status_t vc_create(vc_t** vc_out) {
    mxtl::AutoLock lock(&g_vc_lock);

    mx_status_t status;
    vc_t* vc;
    if ((status = vc_alloc(NULL, g_fb_fd, &vc)) < 0) {
        return status;
    }

    // add to the vc list
    list_add_tail(&g_vc_list, &vc->node);
    g_vc_count++;

    // make this the active vc if it's the first one
    if (!g_active_vc) {
        vc_set_active_console(0);
    } else {
        vc_render(g_active_vc);
    }

    *vc_out = vc;
    return NO_ERROR;
}

static int vc_log_reader_thread(void* arg) {
    auto vc = reinterpret_cast<vc_t*>(arg);
    mx_handle_t h;

    mx_koid_t koid = 0;
    mx_info_handle_basic_t info;
    if (mx_object_get_info(mx_process_self(), MX_INFO_HANDLE_BASIC, &info,
                           sizeof(info), NULL, NULL) == NO_ERROR) {
        koid = info.koid;
    }

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
        // don't print log messages from ourself
        if (rec->pid == koid) {
            continue;
        }
        char tmp[64];
        snprintf(tmp, 64, "\033[32m%05d.%03d\033[39m] \033[31m%05" PRIu64 ".\033[36m%05" PRIu64 "\033[39m> ",
                 (int)(rec->timestamp / 1000000000ULL),
                 (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                 rec->pid, rec->tid);
        vc_write(vc, tmp, strlen(tmp), 0);
        vc_write(vc, rec->data, rec->datalen, 0);
        if ((rec->datalen == 0) || (rec->data[rec->datalen - 1] != '\n')) {
            vc_write(vc, "\n", 1, 0);
        }
    }

    const char* oops = "<<LOG ERROR>>\n";
    vc_write(vc, oops, strlen(oops), 0);

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
                vc_write_status(g_active_vc);
                vc_gfx_invalidate_status(g_active_vc);
            }
        }

        if (length <= 0) {
            printf("vc: read() on battery vc returned %d\n",
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
        printf("vc: failed to open battery vc \"%s\"\n", fn);
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

#if !BUILD_FOR_TEST
int main(int argc, char** argv) {
    int fd;
    for (;;) {
        if ((fd = open("/dev/class/framebuffer/000", O_RDWR)) >= 0) {
            break;
        }
        usleep(100000);
    }

    g_fb_fd = fd;

    // start a thread to listen for new input devices
    int ret = thrd_create_with_name(&g_input_poll_thread,
                                    vc_watch_for_keyboard_devices_thread, NULL,
                                    "vc-inputdev-poll");
    if (ret != thrd_success) {
        xprintf("vc: input polling thread did not start (return value=%d)\n", ret);
    }

    thrd_t u;
    thrd_create_with_name(&u, vc_battery_dir_poll_thread, NULL,
                          "vc-battery-dir-poll");

    vc_t* vc;
    if (vc_create(&vc) == NO_ERROR) {
        thrd_t t;
        thrd_create_with_name(&t, vc_log_reader_thread, vc, "vc-log-reader");
    }

    for (;;) {
        sleep(1000);
    }
    return NO_ERROR;
}
#endif
