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
#include <poll.h>
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
#include <magenta/device/pty.h>
#include <magenta/processargs.h>
#include <port/port.h>
#include <mxio/util.h>
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
                g_active_vc->flags &= ~VC_FLAG_HASOUTPUT;
            }
            vc->active = true;
            vc->flags &= ~VC_FLAG_HASOUTPUT;
            g_active_vc = vc;
            g_active_vc_index = i;
            vc_full_repaint(vc);
            vc_render(vc);
            return NO_ERROR;
        }
        i++;
    }
    return ERR_NOT_FOUND;
}

static int status_width = 0;

void vc_status_update() {
    vc_t* vc = NULL;
    unsigned i = 0;
    int x = 0;

    int w = status_width / (g_vc_count + 1);
    if (w < MIN_TAB_WIDTH) {
        w = MIN_TAB_WIDTH;
    } else if (w > MAX_TAB_WIDTH) {
        w = MAX_TAB_WIDTH;
    }

    char tmp[w];

    vc_status_clear();
    list_for_every_entry (&g_vc_list, vc, vc_t, node) {
        unsigned fg;
        if (vc->active) {
            fg = STATUS_COLOR_ACTIVE;
        } else if (vc->flags & VC_FLAG_HASOUTPUT) {
            fg = STATUS_COLOR_UPDATED;
        } else {
            fg = STATUS_COLOR_DEFAULT;
        }

        int lines = vc_get_scrollback_lines(vc);
        char L = (lines > 0) && (-vc->viewport_y < lines) ? '<' : '[';
        char R = (vc->viewport_y < 0) ? '>' : ']';

        snprintf(tmp, w, "%c%u%c %s", L, i, R, vc->title);
        vc_status_write(x, fg, tmp);
        x += w;
        i++;
    }
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
        vc_full_repaint(g_active_vc);
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
    if (!(vc->flags & VC_FLAG_HASOUTPUT) && !vc->active) {
        vc->flags |= VC_FLAG_HASOUTPUT;
        vc_status_update();
        vc_gfx_invalidate_status();
    }
    return count;
}

// Create a new vc_t and add it to the console list.
static mx_status_t vc_create(vc_t** vc_out) {
    mx_status_t status;
    vc_t* vc;
    if ((status = vc_alloc(&vc)) < 0) {
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

static int g_fb_fd;

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
        if (vc->fd >= 0) {
            write(vc->fd, output, length);
        }
        vc_scroll_viewport_bottom(vc);
    }
}

static int input_watcher_thread(void* arg) {
    vc_watch_for_keyboard_devices(handle_key_press);
    return -1;
}

static vc_t* log_vc;
static mx_koid_t proc_koid;

static mx_status_t log_reader_cb(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    char buf[MX_LOG_RECORD_MAX];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    mx_status_t status;
    for (;;) {
        if ((status = mx_log_read(ph->handle, MX_LOG_RECORD_MAX, rec, 0)) < 0) {
            if (status == ERR_SHOULD_WAIT) {
                return NO_ERROR;
            }
            break;
        }
        // don't print log messages from ourself
        if (rec->pid == proc_koid) {
            continue;
        }
        char tmp[64];
        snprintf(tmp, 64, "\033[32m%05d.%03d\033[39m] \033[31m%05" PRIu64 ".\033[36m%05" PRIu64 "\033[39m> ",
                 (int)(rec->timestamp / 1000000000ULL),
                 (int)((rec->timestamp / 1000000ULL) % 1000ULL),
                 rec->pid, rec->tid);
        {
            mxtl::AutoLock lock(&g_vc_lock);
            vc_write(log_vc, tmp, strlen(tmp), 0);
            vc_write(log_vc, rec->data, rec->datalen, 0);
            if ((rec->datalen == 0) ||
                (rec->data[rec->datalen - 1] != '\n')) {
                vc_write(log_vc, "\n", 1, 0);
            }
        }
    }

    const char* oops = "<<LOG ERROR>>\n";
    mxtl::AutoLock lock(&g_vc_lock);
    vc_write(log_vc, oops, strlen(oops), 0);

    return status;
}

static port_t port;
static port_handler_t ownership_ph;
static port_handler_t log_ph;
static port_handler_t new_vc_ph;

static mx_status_t launch_shell(vc_t* vc, int fd) {
    const char* args[] = { "/boot/bin/sh" };

    launchpad_t* lp;
    launchpad_create(mx_job_default(), "vc:sh", &lp);
    launchpad_load_from_file(lp, args[0]);
    launchpad_set_args(lp, 1, args);
    launchpad_transfer_fd(lp, fd, MXIO_FLAG_USE_FOR_STDIO | 0);
    launchpad_clone(lp, LP_CLONE_MXIO_ROOT | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);

    const char* errmsg;
    mx_status_t r;
    if ((r = launchpad_go(lp, &vc->proc, &errmsg)) < 0) {
        printf("vc: cannot spawn shell: %s: %d\n", errmsg, r);
    }
    return r;
}

static void session_destroy(vc_t* vc) {
    mxtl::AutoLock lock(&g_vc_lock);
    if (vc->fd >= 0) {
        port_fd_handler_done(&vc->fh);
        // vc_destroy() closes the fd
    }
    if (vc->proc != MX_HANDLE_INVALID) {
        mx_task_kill(vc->proc);
    }
    vc_destroy(vc);
}

static mx_status_t session_io_cb(port_fd_handler_t* fh, unsigned pollevt, uint32_t evt) {
    vc_t* vc = containerof(fh, vc_t, fh);

    if (pollevt & POLLIN) {
        size_t count = 0;
        for (;;) {
            char data[8192];
            ssize_t r = read(vc->fd, data, sizeof(data));
            if (r <= 0) {
                break;
            }
            count += r;
            {
                mxtl::AutoLock lock(&g_vc_lock);
                vc_write(vc, data, r, 0);
            }
        }
        if (count) {
            return NO_ERROR;
        }
    }

    if (pollevt & (POLLRDHUP | POLLHUP)) {
        // shell sessions get restarted on exit
        if (vc->is_shell) {
            mx_task_kill(vc->proc);
            vc->proc = MX_HANDLE_INVALID;

            int fd = openat(vc->fd, "0", O_RDWR);
            if (fd < 0) {
                goto fail;
            }

            if(launch_shell(vc, fd) < 0) {
                goto fail;
            }
            return NO_ERROR;
        }
    }

fail:
    session_destroy(vc);
    return ERR_STOP;
}

static mx_status_t session_create(vc_t** out, int* out_fd, bool make_active) {
    int fd;

    // The ptmx device can start later than these threads
    int retry = 30;
    while ((fd = open("/dev/misc/ptmx", O_RDWR | O_NONBLOCK)) < 0) {
        if (--retry == 0) {
            return ERR_IO;
        }
        usleep(100000);
    }

    int client_fd = openat(fd, "0", O_RDWR);
    if (client_fd < 0) {
        close(fd);
        return ERR_IO;
    }

    vc_t* vc;
    {
        mxtl::AutoLock lock(&g_vc_lock);
        if (vc_create(&vc)) {
            close(fd);
            close(client_fd);
            return ERR_INTERNAL;
        }
        mx_status_t r;
        if ((r = port_fd_handler_init(&vc->fh, fd, POLLIN | POLLRDHUP | POLLHUP)) < 0) {
            vc_destroy(vc);
            close(fd);
            close(client_fd);
            return r;
        }
        vc->fd = fd;

        if (make_active) {
            vc_set_active(-1, vc);
        }
    }

    pty_window_size_t wsz = {
        .width = vc->columns,
        .height = vc->rows,
    };
    ioctl_pty_set_window_size(fd, &wsz);

    vc->fh.func = session_io_cb;

    *out = vc;
    *out_fd = client_fd;
    return NO_ERROR;
}

static void start_shell(bool make_active) {
    vc_t* vc;
    int fd;

    if (session_create(&vc, &fd, make_active) < 0) {
        return;
    }

    vc->is_shell = true;

    if (launch_shell(vc, fd) < 0) {
        session_destroy(vc);
    } else {
        port_wait(&port, &vc->fh.ph);
    }
}

static mx_status_t new_vc_cb(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    mx_handle_t h;
    uint32_t dcount, hcount;
    if (mx_channel_read(ph->handle, 0, NULL, &h, 0, 1, &dcount, &hcount) < 0) {
        return NO_ERROR;
    }
    if (hcount != 1) {
        return NO_ERROR;
    }

    vc_t* vc;
    int fd;
    if (session_create(&vc, &fd, true) < 0) {
        mx_handle_close(h);
        return NO_ERROR;
    }

    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    mx_status_t r = mxio_transfer_fd(fd, MXIO_FLAG_USE_FOR_STDIO | 0, handles, types);
    if ((r != 2) || (mx_channel_write(h, 0, types, 2 * sizeof(uint32_t), handles, 2) < 0)) {
        for (int n = 0; n < r; n++) {
            mx_handle_close(handles[n]);
        }
        session_destroy(vc);
    } else {
        port_wait(&port, &vc->fh.ph);
    }

    mx_handle_close(h);
    return NO_ERROR;
}


static mx_status_t ownership_ph_cb(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    mxtl::AutoLock lock(&g_vc_lock);

    // If we owned it, we've been notified of losing it, or the other way 'round
    g_vc_owns_display = !g_vc_owns_display;

    // If we've gained it, repaint
    // In both cases adjust waitfor to wait for the opposite
    if (g_vc_owns_display) {
        ph->waitfor = MX_USER_SIGNAL_1;
        if (g_active_vc) {
            vc_gfx_invalidate_all(g_active_vc);
        }
    } else {
        ph->waitfor = MX_USER_SIGNAL_0;
    }

    return NO_ERROR;
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

    if (port_init(&port) < 0) {
        return -1;
    }

    int fd;
    for (;;) {
        if ((fd = open("/dev/class/framebuffer/000/virtcon", O_RDWR)) >= 0) {
            break;
        }
        usleep(100000);
    }
    if (vc_init_gfx(fd) < 0) {
        return -1;
    }

    g_fb_fd = fd;

    // create initial console for debug log
    if (vc_create(&log_vc) != NO_ERROR) {
        return -1;
    }
    status_width = log_vc->columns;
    snprintf(log_vc->title, sizeof(log_vc->title), "debuglog");

    // Get our process koid so the log reader can
    // filter out our own debug messages from the log
    mx_info_handle_basic_t info;
    if (mx_object_get_info(mx_process_self(), MX_INFO_HANDLE_BASIC, &info,
                           sizeof(info), NULL, NULL) == NO_ERROR) {
        proc_koid = info.koid;
    }

    // TODO: receive from launching process
    if (mx_log_create(MX_LOG_FLAG_READABLE, &log_ph.handle) < 0) {
        printf("vc log listener: cannot open log\n");
        return -1;
    }

    log_ph.func = log_reader_cb;
    log_ph.waitfor = MX_LOG_READABLE;
    port_wait(&port, &log_ph);

    if ((new_vc_ph.handle = mx_get_startup_handle(PA_HND(PA_USER0, 0))) != MX_HANDLE_INVALID) {
        new_vc_ph.func = new_vc_cb;
        new_vc_ph.waitfor = MX_CHANNEL_READABLE;
        port_wait(&port, &new_vc_ph);
    }

    // start a thread to listen for new input devices
    thrd_t t;
    int ret = thrd_create_with_name(&t, input_watcher_thread, NULL,
                                    "vc-input-watcher");
    if (ret != thrd_success) {
        xprintf("vc: input polling thread did not start (return value=%d)\n", ret);
    }

    setenv("TERM", "xterm", 1);

    start_shell(keep_log ? false : true);
    start_shell(false);
    start_shell(false);

    mx_handle_t e = MX_HANDLE_INVALID;
    ioctl_display_get_ownership_change_event(fd, &e);

    if (e != MX_HANDLE_INVALID) {
        ownership_ph.func = ownership_ph_cb;
        ownership_ph.handle = e;
        ownership_ph.waitfor = MX_USER_SIGNAL_1;
        port_wait(&port, &ownership_ph);
    }

    mx_status_t r = port_dispatch(&port, MX_TIME_INFINITE, false);
    printf("vc: port failure: %d\n", r);
    return -1;
}
#endif
