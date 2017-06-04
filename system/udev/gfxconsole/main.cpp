// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <gfx/gfx.h>
#include <hid/usages.h>
#include <magenta/device/console.h>
#include <magenta/device/display.h>
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

#if !BUILD_FOR_TEST
#include <launchpad/launchpad.h>
#endif

#define VCDEBUG 1

#include "keyboard-vt100.h"
#include "keyboard.h"
#include "vc.h"
#include "vcdebug.h"

static struct list_node g_vc_list = LIST_INITIAL_VALUE(g_vc_list);
static unsigned g_vc_count = 0;
static vc_t* g_active_vc;
static unsigned g_active_vc_index;
static vc_battery_info_t g_battery_info;

static mx_status_t vc_set_active(int num, vc_t* vc);

static void vc_toggle_framebuffer();


// Process key sequences that affect the console (scrolling, switching
// console, etc.) without sending input to the current console.  This
// returns whether this key press was handled.
static bool vc_handle_control_keys(uint8_t keycode, int modifiers) {
    switch (keycode) {
    case HID_USAGE_KEY_F1 ... HID_USAGE_KEY_F10:
        if (modifiers & MOD_ALT) {
            vc_set_active(keycode - HID_USAGE_KEY_F1, NULL);
            return true;
        }
        break;

    case HID_USAGE_KEY_TAB:
        if (modifiers & MOD_ALT) {
            if (modifiers & MOD_SHIFT) {
                vc_set_active(g_active_vc_index == 0 ? g_vc_count - 1 : g_active_vc_index - 1, NULL);
            } else {
                vc_set_active(g_active_vc_index == g_vc_count - 1 ? 0 : g_active_vc_index + 1, NULL);
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
static bool vc_handle_device_control_keys(uint8_t keycode, int modifiers){
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

static mx_status_t vc_set_active(int num, vc_t* to_vc) {
    vc_t* vc = NULL;
    int i = 0;
    list_for_every_entry (&g_vc_list, vc, vc_t, node) {
        if ((num == i) || (to_vc == vc)) {
            if (vc == g_active_vc) {
                return NO_ERROR;
            }
            if (g_active_vc) {
                g_active_vc->active = false;
            }
            vc->active = true;
            vc->flags &= ~VC_FLAG_HASOUTPUT;
            g_active_vc = vc;
            g_active_vc_index = i;
            vc_render(vc);
            return NO_ERROR;
        }
        i++;
    }
    return ERR_NOT_FOUND;
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
    list_delete(&vc->node);
    g_vc_count -= 1;

    if (vc->active) {
        g_active_vc = NULL;
        if (g_active_vc_index >= g_vc_count) {
            g_active_vc_index = g_vc_count - 1;
        }
        vc_set_active(g_active_vc_index, NULL);
    } else if (g_active_vc) {
        vc_render(g_active_vc);
    }

    vc_free(vc);
}

ssize_t vc_write(vc_t* vc, const void* buf, size_t count, mx_off_t off) {
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
        vc_set_active(-1, vc);
    } else {
        vc_render(g_active_vc);
    }

    *vc_out = vc;
    return NO_ERROR;
}

#if BUILD_FOR_TEST
static void vc_toggle_framebuffer() {
}
#else

// The entire vc_*() world is single threaded.
// All the threads below this point acquire the g_vc_lock
// before calling into the vc world
//
// TODO: convert this pile of threads into a single-threaded,
// ports-based event handler and remove g_vc_lock entirely.

static mtx_t g_vc_lock = MTX_INIT;

// remember whether the virtual console controls the display
static bool g_vc_owns_display = true;

static void vc_toggle_framebuffer() {
    uint32_t n = g_vc_owns_display ? 1 : 0;
    ioctl_display_set_owner(g_fb_fd, &n);
}

static void handle_key_press(uint8_t keycode, int modifiers) {
    mxtl::AutoLock lock(&g_vc_lock);

    // Handle vc-level control keys
    if (vc_handle_device_control_keys(keycode, modifiers))
        return;

    // Handle other keys only if we own the display
    if (!g_vc_owns_display)
        return;

    // Handle other control keys
    if (vc_handle_control_keys(keycode, modifiers))
        return;

    vc_t* vc = g_active_vc;
    char output[4];
    uint32_t length = hid_key_to_vt100_code(
        keycode, modifiers, vc->keymap, output, sizeof(output));
    if (length > 0) {
        if (vc->client_fd >= 0) {
            write(vc->client_fd, output, length);
        }
        vc_scroll_viewport_bottom(vc);
    }
}

static int input_watcher_thread(void* arg) {
    vc_watch_for_keyboard_devices(handle_key_press);
    return -1;
}

static int log_reader_thread(void* arg) {
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
        {
            mxtl::AutoLock lock(&g_vc_lock);
            vc_write(vc, tmp, strlen(tmp), 0);
            vc_write(vc, rec->data, rec->datalen, 0);
            if ((rec->datalen == 0) ||
                (rec->data[rec->datalen - 1] != '\n')) {
                vc_write(vc, "\n", 1, 0);
            }
        }
    }

    const char* oops = "<<LOG ERROR>>\n";
    mxtl::AutoLock lock(&g_vc_lock);
    vc_write(vc, oops, strlen(oops), 0);

    return 0;
}

static int battery_poll_thread(void* arg) {
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

static mx_status_t battery_device_added(int dirfd, int event, const char* fn, void* cookie) {
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
    int rc = thrd_create_with_name(&t, battery_poll_thread,
        reinterpret_cast<void*>(static_cast<uintptr_t>(battery_fd)),
        "vc-battery-poll");
    if (rc != thrd_success) {
        close(battery_fd);
        return -1;
    }
    thrd_detach(t);
    return NO_ERROR;
}

static int battery_watcher_thread(void* arg) {
    int dirfd;
    if ((dirfd = open("/dev/class/battery", O_DIRECTORY | O_RDONLY)) < 0) {
        return -1;
    }
    mxio_watch_directory(dirfd, battery_device_added, NULL);
    close(dirfd);
    return 0;
}

#include <magenta/device/pty.h>

int mkpty(vc_t* vc, int fd[2]) {
    fd[0] = open("/dev/misc/ptmx", O_RDWR);
    if (fd[0] < 0) {
        return -1;
    }
    fd[1] = openat(fd[0], "0", O_RDWR);
    if (fd[1] < 0) {
        close(fd[0]);
        return -1;
    }
    pty_window_size_t wsz = {
        .width = vc->columns,
        .height = vc->rows,
    };
    ioctl_pty_set_window_size(fd[0], &wsz);

    return 0;
}

static int _shell_thread(void* arg, bool make_active) {
    vc_t* vc;
    {
        mxtl::AutoLock lock(&g_vc_lock);
        if (vc_create(&vc)) {
            return 0;
        }
    }

    if ((vc->client_fd = open("/dev/misc/ptmx", O_RDWR)) < 0) {
        goto done;
    }

    for (;;) {
        int fd[2];
        if (mkpty(vc, fd) < 0) {
            mxtl::AutoLock lock(&g_vc_lock);
            vc_destroy(vc);
            return 0;
        }

        const char* args[] = { "/boot/bin/sh" };

        launchpad_t* lp;
        launchpad_create(mx_job_default(), "vc:sh", &lp);
        launchpad_load_from_file(lp, args[0]);
        launchpad_set_args(lp, 1, args);
        launchpad_transfer_fd(lp, fd[1], MXIO_FLAG_USE_FOR_STDIO | 0);
        launchpad_clone(lp, LP_CLONE_MXIO_ROOT | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);

        const char* errmsg;
        mx_handle_t proc;
        mx_status_t r;
        if ((r = launchpad_go(lp, &proc, &errmsg)) < 0) {
            printf("vc: cannot spawn shell: %s: %d\n", errmsg, r);
            close(fd[0]);
            goto done;
        }

        if (make_active) {
            // only do this the first time, not on shell restart
            make_active = false;
            mxtl::AutoLock lock(&g_vc_lock);
            vc_set_active(-1, vc);
        }

        vc->client_fd = fd[0];

        for (;;) {
            char data[8192];
            ssize_t r = read(vc->client_fd, data, sizeof(data));
            if (r <= 0) {
                break;
            }
            {
                mxtl::AutoLock lock(&g_vc_lock);
                vc_write(vc, data, r, 0);
            }
        }

        {
            mxtl::AutoLock lock(&g_vc_lock);
            vc->client_fd = -1;
        }
        close(fd[0]);

        mx_task_kill(proc);
    }

done:
    mxtl::AutoLock lock(&g_vc_lock);
    vc_destroy(vc);
    return 0;
}

static int shell_thread(void* arg) {
    return _shell_thread(arg, false);
}

static int shell_thread_1st(void* arg) {
    return _shell_thread(arg, true);
}

static void set_owns_display(bool acquired) {
    mxtl::AutoLock lock(&g_vc_lock);
    g_vc_owns_display = acquired;
    if (acquired && g_active_vc) {
        vc_gfx_invalidate_all(g_active_vc);
    }
}

int main(int argc, char** argv) {
    bool keep_log = false;
    while (argc > 1) {
        if (!strcmp(argv[1],"--keep-log-active")) {
            keep_log = true;
        }
        argc--;
        argv++;
    }

    int fd;
    for (;;) {
        if ((fd = open("/dev/class/framebuffer/000/virtcon", O_RDWR)) >= 0) {
            break;
        }
        usleep(100000);
    }

    g_fb_fd = fd;

    // create initial console for debug log
    vc_t* vc;
    {
        mxtl::AutoLock lock(&g_vc_lock);
        if (vc_create(&vc) != NO_ERROR) {
            return -1;
        }
    }
    thrd_t t;
    thrd_create_with_name(&t, log_reader_thread, vc, "vc-log-reader");

    // start a thread to listen for new input devices
    int ret = thrd_create_with_name(&t, input_watcher_thread, NULL,
                                    "vc-input-watcher");
    if (ret != thrd_success) {
        xprintf("vc: input polling thread did not start (return value=%d)\n", ret);
    }

    thrd_create_with_name(&t, battery_watcher_thread, NULL,
                          "vc-battery-watcher");

    setenv("TERM", "xterm", 1);

    thrd_create_with_name(&t, keep_log ? shell_thread : shell_thread_1st, vc, "vc-shell-reader");
    thrd_create_with_name(&t, shell_thread, vc, "vc-shell-reader");
    thrd_create_with_name(&t, shell_thread, vc, "vc-shell-reader");

    mx_handle_t e = MX_HANDLE_INVALID;
    ioctl_display_get_ownership_change_event(fd, &e);

    for (;;) {
        mx_status_t r;
        if (g_vc_owns_display) {
            if ((r = mx_object_wait_one(e, MX_USER_SIGNAL_1, MX_TIME_INFINITE, NULL)) < 0) {
                if (r != ERR_TIMED_OUT) {
                    break;
                }
            }
            set_owns_display(false);
        } else {
            if ((r = mx_object_wait_one(e, MX_USER_SIGNAL_0, MX_TIME_INFINITE, NULL)) < 0) {
                if (r != ERR_TIMED_OUT) {
                    break;
                }
            }
            set_owns_display(true);
        }
    }

    //TODO: wait for and acquire a new display
    printf("vc: DISCONNECT\n");
    return 0;
}
#endif
