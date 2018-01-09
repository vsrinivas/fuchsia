// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <launchpad/launchpad.h>

#include <zircon/device/pty.h>
#include <zircon/device/vfs.h>
#include <zircon/device/display.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/object.h>

#include <fdio/io.h>
#include <fdio/util.h>
#include <fdio/watcher.h>

#include <port/port.h>

#include "vc.h"

port_t port;
static port_handler_t ownership_ph;
static port_handler_t log_ph;
static port_handler_t new_vc_ph;
static port_handler_t input_ph;
static port_handler_t fb_ph;

static int input_dir_fd;
static int fb_dir_fd;

static vc_t* log_vc;
static zx_koid_t proc_koid;

static int g_fb_fd = -1;
#define FB_NAME_LEN 32
static char g_fb_name[FB_NAME_LEN + 1];

static zx_status_t fb_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt);
static zx_status_t ownership_ph_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt);

// remember whether the virtual console controls the display
bool g_vc_owns_display = true;

void vc_toggle_framebuffer() {
    if (g_fb_fd != -1) {
        uint32_t n = g_vc_owns_display ? 1 : 0;
        ioctl_display_set_owner(g_fb_fd, &n);
    }
}

static zx_status_t log_reader_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    char buf[ZX_LOG_RECORD_MAX];
    zx_log_record_t* rec = (zx_log_record_t*)buf;
    zx_status_t status;
    for (;;) {
        if ((status = zx_log_read(ph->handle, ZX_LOG_RECORD_MAX, rec, 0)) < 0) {
            if (status == ZX_ERR_SHOULD_WAIT) {
                // return non-OK to avoid needlessly re-arming the repeating wait
                return ZX_ERR_NEXT;
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
        vc_write(log_vc, tmp, strlen(tmp), 0);
        vc_write(log_vc, rec->data, rec->datalen, 0);
        if ((rec->datalen == 0) ||
            (rec->data[rec->datalen - 1] != '\n')) {
            vc_write(log_vc, "\n", 1, 0);
        }
    }

    const char* oops = "<<LOG ERROR>>\n";
    vc_write(log_vc, oops, strlen(oops), 0);

    // Error reading the log, no point in continuing to try to read
    // log messages.
    port_cancel(&port, &log_ph);
    return status;
}

static zx_status_t launch_shell(vc_t* vc, int fd, const char* cmd) {
    const char* args[] = { "/boot/bin/sh", "-c", cmd };

    launchpad_t* lp;
    launchpad_create(zx_job_default(), "vc:sh", &lp);
    launchpad_load_from_file(lp, args[0]);
    launchpad_set_args(lp, cmd ? 3 : 1, args);
    launchpad_transfer_fd(lp, fd, FDIO_FLAG_USE_FOR_STDIO | 0);
    launchpad_clone(lp, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);

    const char* errmsg;
    zx_status_t r;
    if ((r = launchpad_go(lp, &vc->proc, &errmsg)) < 0) {
        printf("vc: cannot spawn shell: %s: %d\n", errmsg, r);
    }
    return r;
}

static void session_destroy(vc_t* vc) {
    if (vc->fd >= 0) {
        port_fd_handler_done(&vc->fh);
        // vc_destroy() closes the fd
    }
    if (vc->proc != ZX_HANDLE_INVALID) {
        zx_task_kill(vc->proc);
    }
    vc_destroy(vc);
}

static zx_status_t session_io_cb(port_fd_handler_t* fh, unsigned pollevt, uint32_t evt) {
    vc_t* vc = containerof(fh, vc_t, fh);

    if (pollevt & POLLIN) {
        char data[1024];
        ssize_t r = read(vc->fd, data, sizeof(data));
        if (r > 0) {
            vc_write(vc, data, r, 0);
            return ZX_OK;
        }
    }

    if (pollevt & (POLLRDHUP | POLLHUP)) {
        // shell sessions get restarted on exit
        if (vc->is_shell) {
            zx_task_kill(vc->proc);
            vc->proc = ZX_HANDLE_INVALID;

            int fd = openat(vc->fd, "0", O_RDWR);
            if (fd < 0) {
                goto fail;
            }

            if(launch_shell(vc, fd, NULL) < 0) {
                goto fail;
            }
            return ZX_OK;
        }
    }

fail:
    session_destroy(vc);
    return ZX_ERR_STOP;
}

static zx_status_t session_create(vc_t** out, int* out_fd, bool make_active, bool special) {
    int fd;

    // The ptmx device can start later than these threads
    int retry = 30;
    while ((fd = open("/dev/misc/ptmx", O_RDWR | O_NONBLOCK)) < 0) {
        if (--retry == 0) {
            return ZX_ERR_IO;
        }
        usleep(100000);
    }

    int client_fd = openat(fd, "0", O_RDWR);
    if (client_fd < 0) {
        close(fd);
        return ZX_ERR_IO;
    }

    vc_t* vc;
    if (vc_create(&vc, special)) {
        close(fd);
        close(client_fd);
        return ZX_ERR_INTERNAL;
    }
    zx_status_t r;
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

    pty_window_size_t wsz = {
        .width = vc->columns,
        .height = vc->rows,
    };
    ioctl_pty_set_window_size(fd, &wsz);

    vc->fh.func = session_io_cb;

    *out = vc;
    *out_fd = client_fd;
    return ZX_OK;
}

static void start_shell(bool make_active, const char* cmd) {
    vc_t* vc;
    int fd;

    if (session_create(&vc, &fd, make_active, cmd != NULL) < 0) {
        return;
    }

    vc->is_shell = true;

    if (launch_shell(vc, fd, cmd) < 0) {
        session_destroy(vc);
    } else {
        port_wait(&port, &vc->fh.ph);
    }
}

static zx_status_t new_vc_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    zx_handle_t h;
    uint32_t dcount, hcount;
    if (zx_channel_read(ph->handle, 0, NULL, &h, 0, 1, &dcount, &hcount) < 0) {
        return ZX_OK;
    }
    if (hcount != 1) {
        return ZX_OK;
    }

    vc_t* vc;
    int fd;
    if (session_create(&vc, &fd, true, false) < 0) {
        zx_handle_close(h);
        return ZX_OK;
    }

    zx_handle_t handles[FDIO_MAX_HANDLES];
    uint32_t types[FDIO_MAX_HANDLES];
    zx_status_t r = fdio_transfer_fd(fd, FDIO_FLAG_USE_FOR_STDIO | 0, handles, types);
    if ((r != 2) || (zx_channel_write(h, 0, types, 2 * sizeof(uint32_t), handles, 2) < 0)) {
        for (int n = 0; n < r; n++) {
            zx_handle_close(handles[n]);
        }
        session_destroy(vc);
    } else {
        port_wait(&port, &vc->fh.ph);
    }

    zx_handle_close(h);
    return ZX_OK;
}

static void input_dir_event(unsigned evt, const char* name) {
    if ((evt != VFS_WATCH_EVT_EXISTING) && (evt != VFS_WATCH_EVT_ADDED)) {
        return;
    }

    printf("vc: new input device /dev/class/input/%s\n", name);

    int fd;
    if ((fd = openat(input_dir_fd, name, O_RDONLY)) < 0) {
        return;
    }

    new_input_device(fd, handle_key_press);
}

static void setup_dir_watcher(const char* dir,
                              zx_status_t (*cb)(port_handler_t*, zx_signals_t, uint32_t),
                              port_handler_t* ph,
                              int* fd_out) {
    if ((*fd_out = open(dir, O_DIRECTORY | O_RDONLY)) >= 0) {
        vfs_watch_dir_t wd;
        wd.mask = VFS_WATCH_MASK_ALL;
        wd.options = 0;
        if (zx_channel_create(0, &wd.channel, &ph->handle) == ZX_OK) {
            if ((ioctl_vfs_watch_dir(*fd_out, &wd)) == ZX_OK) {
                ph->waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
                ph->func = cb;
                port_wait(&port, ph);
            } else {
                zx_handle_close(wd.channel);
                zx_handle_close(ph->handle);
                close(*fd_out);
                *fd_out = -1;
            }
        } else {
            close(*fd_out);
            *fd_out = -1;
        }
    }
}

static void fb_dir_event(unsigned evt, const char* name) {
    if (strlen(name) > FB_NAME_LEN) {
        printf("vc: truncating long framebuffer name (\"%s\")\n", name);
    }

    // If we're already connected to a display, ignore events on other displays
    if (g_fb_fd != -1 && strncmp(g_fb_name, name, FB_NAME_LEN)) {
        return;
    }

    if (evt == VFS_WATCH_EVT_ADDED || evt == VFS_WATCH_EVT_EXISTING) {
        strncpy(g_fb_name, name, FB_NAME_LEN);
        g_fb_name[FB_NAME_LEN] = '\0';

        int fd;
        char file_name[sizeof("/dev/class/framebuffer//virtcon") + FB_NAME_LEN];
        snprintf(file_name, sizeof(file_name), "/dev/class/framebuffer/%s/virtcon", g_fb_name);
        if ((fd = open(file_name, O_RDWR)) < 0) {
            printf("vc: failed to open display \"%s\": %d\n", file_name, fd);
            return;
        }

        if (vc_init_gfx(fd) < 0) {
            printf("vc: failed to initialize graphics for new display\n");
            return;
        }

        g_fb_fd = fd;

        zx_handle_t e = ZX_HANDLE_INVALID;
        ioctl_display_get_ownership_change_event(fd, &e);

        if (e != ZX_HANDLE_INVALID) {
            ownership_ph.func = ownership_ph_cb;
            ownership_ph.handle = e;
            ownership_ph.waitfor = ZX_USER_SIGNAL_1;
            port_wait(&port, &ownership_ph);
        }

        // Only listen for logs when we have somewhere to print them. Also,
        // use a repeating wait so that we don't add/remove observers for each
        // log message (which is helpful when tracing the addition/removal of
        // observers).
        port_wait_repeating(&port, &log_ph);
        vc_show_active();
    } else if (evt == VFS_WATCH_EVT_REMOVED) {
        close(g_fb_fd);
        g_fb_fd = -1;

        port_cancel(&port, &log_ph);
        vc_free_gfx();

        // Remove and re-add the framebuffer watcher to handle the case where
        // a second display was already attached (with the EXISTING event).
        port_cancel(&port, &fb_ph);
        close(fb_dir_fd);
        setup_dir_watcher("/dev/class/framebuffer", fb_cb, &fb_ph, &fb_dir_fd);
    }
}

static bool handle_dir_event(port_handler_t* ph, zx_signals_t signals,
                             void (*event_handler)(unsigned event, const char* msg)) {
    if (!(signals & ZX_CHANNEL_READABLE)) {
        return false;
    }

    // Buffer contains events { Opcode, Len, Name[Len] }
    // See zircon/device/vfs.h for more detail
    // extra byte is for temporary NUL
    uint8_t buf[VFS_WATCH_MSG_MAX + 1];
    uint32_t len;
    if (zx_channel_read(ph->handle, 0, buf, NULL, sizeof(buf) - 1, 0, &len, NULL) < 0) {
        return false;
    }

    uint8_t* msg = buf;
    while (len >= 2) {
        uint8_t event = *msg++;
        uint8_t namelen = *msg++;
        if (len < (namelen + 2u)) {
            return false;
        }
        // add temporary nul
        uint8_t tmp = msg[namelen];
        msg[namelen] = 0;
        event_handler(event, (char*) msg);
        msg[namelen] = tmp;
        msg += namelen;
        len -= (namelen + 2u);
    }
    return true;
}

static zx_status_t input_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    if (!handle_dir_event(ph, signals, input_dir_event)) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

static zx_status_t fb_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    if (!handle_dir_event(ph, signals, fb_dir_event)) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

static zx_status_t ownership_ph_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    // If we owned it, we've been notified of losing it, or the other way 'round
    g_vc_owns_display = !g_vc_owns_display;

    // If we've gained it, repaint
    // In both cases adjust waitfor to wait for the opposite
    if (g_vc_owns_display) {
        ph->waitfor = ZX_USER_SIGNAL_1;
        if (g_active_vc) {
            vc_flush_all(g_active_vc);
        }
    } else {
        ph->waitfor = ZX_USER_SIGNAL_0;
    }

    return ZX_OK;
}

int main(int argc, char** argv) {
    // NOTE: devmgr has getenv_bool. when more options are added, consider
    // sharing that.
    bool keep_log = false;
    const char* value = getenv("virtcon.keep-log-visible");
    if (value == NULL ||
        ((strcmp(value, "0") == 0) ||
        (strcmp(value, "false") == 0) ||
        (strcmp(value, "off") == 0))) {
        keep_log = false;
    } else {
        keep_log = true;
    }

    const char* cmd = NULL;
    while (argc > 1) {
        if (!strcmp(argv[1], "--run")) {
            if (argc > 2) {
                argc--;
                argv++;
                cmd = argv[1];
                printf("CMD: %s\n", cmd);
            }
        }
        argc--;
        argv++;
    }

    if (port_init(&port) < 0) {
        return -1;
    }

    // create initial console for debug log
    if (vc_create(&log_vc, false) != ZX_OK) {
        return -1;
    }
    snprintf(log_vc->title, sizeof(log_vc->title), "debuglog");

    // Get our process koid so the log reader can
    // filter out our own debug messages from the log
    zx_info_handle_basic_t info;
    if (zx_object_get_info(zx_process_self(), ZX_INFO_HANDLE_BASIC, &info,
                           sizeof(info), NULL, NULL) == ZX_OK) {
        proc_koid = info.koid;
    }

    // TODO: receive from launching process
    if (zx_log_create(ZX_LOG_FLAG_READABLE, &log_ph.handle) < 0) {
        printf("vc log listener: cannot open log\n");
        return -1;
    }

    log_ph.func = log_reader_cb;
    log_ph.waitfor = ZX_LOG_READABLE;

    if ((new_vc_ph.handle = zx_get_startup_handle(PA_HND(PA_USER0, 0))) != ZX_HANDLE_INVALID) {
        new_vc_ph.func = new_vc_cb;
        new_vc_ph.waitfor = ZX_CHANNEL_READABLE;
        port_wait(&port, &new_vc_ph);
    }

    setup_dir_watcher("/dev/class/input", input_cb, &input_ph, &input_dir_fd);
    setup_dir_watcher("/dev/class/framebuffer", fb_cb, &fb_ph, &fb_dir_fd);

    setenv("TERM", "xterm", 1);

    start_shell(keep_log ? false : true, cmd);
    start_shell(false, NULL);
    start_shell(false, NULL);

    zx_status_t r = port_dispatch(&port, ZX_TIME_INFINITE, false);
    printf("vc: port failure: %d\n", r);
    return -1;
}
