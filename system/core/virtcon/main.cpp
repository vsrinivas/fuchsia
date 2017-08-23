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

#include <magenta/device/pty.h>
#include <magenta/device/vfs.h>
#include <magenta/device/display.h>
#include <magenta/listnode.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <magenta/syscalls/object.h>

#include <mxio/io.h>
#include <mxio/util.h>
#include <mxio/watcher.h>

#include <port/port.h>

#include "vc.h"

port_t port;
static port_handler_t ownership_ph;
static port_handler_t log_ph;
static port_handler_t new_vc_ph;
static port_handler_t input_ph;

static int input_dir_fd;

static vc_t* log_vc;
static mx_koid_t proc_koid;

static int g_fb_fd = -1;

// remember whether the virtual console controls the display
bool g_vc_owns_display = true;

void vc_toggle_framebuffer() {
    uint32_t n = g_vc_owns_display ? 1 : 0;
    ioctl_display_set_owner(g_fb_fd, &n);
}

static mx_status_t log_reader_cb(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    char buf[MX_LOG_RECORD_MAX];
    mx_log_record_t* rec = (mx_log_record_t*)buf;
    mx_status_t status;
    for (;;) {
        if ((status = mx_log_read(ph->handle, MX_LOG_RECORD_MAX, rec, 0)) < 0) {
            if (status == MX_ERR_SHOULD_WAIT) {
                return MX_OK;
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

    return status;
}

static mx_status_t launch_shell(vc_t* vc, int fd, const char* cmd) {
    const char* args[] = { "/boot/bin/sh", "-c", cmd };

    launchpad_t* lp;
    launchpad_create(mx_job_default(), "vc:sh", &lp);
    launchpad_load_from_file(lp, args[0]);
    launchpad_set_args(lp, cmd ? 3 : 1, args);
    launchpad_transfer_fd(lp, fd, MXIO_FLAG_USE_FOR_STDIO | 0);
    launchpad_clone(lp, LP_CLONE_MXIO_NAMESPACE | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);

    const char* errmsg;
    mx_status_t r;
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
    if (vc->proc != MX_HANDLE_INVALID) {
        mx_task_kill(vc->proc);
    }
    vc_destroy(vc);
}

static mx_status_t session_io_cb(port_fd_handler_t* fh, unsigned pollevt, uint32_t evt) {
    vc_t* vc = containerof(fh, vc_t, fh);

    if (pollevt & POLLIN) {
        char data[1024];
        ssize_t r = read(vc->fd, data, sizeof(data));
        if (r > 0) {
            vc_write(vc, data, r, 0);
            return MX_OK;
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

            if(launch_shell(vc, fd, NULL) < 0) {
                goto fail;
            }
            return MX_OK;
        }
    }

fail:
    session_destroy(vc);
    return MX_ERR_STOP;
}

static mx_status_t session_create(vc_t** out, int* out_fd, bool make_active, bool special) {
    int fd;

    // The ptmx device can start later than these threads
    int retry = 30;
    while ((fd = open("/dev/misc/ptmx", O_RDWR | O_NONBLOCK)) < 0) {
        if (--retry == 0) {
            return MX_ERR_IO;
        }
        usleep(100000);
    }

    int client_fd = openat(fd, "0", O_RDWR);
    if (client_fd < 0) {
        close(fd);
        return MX_ERR_IO;
    }

    vc_t* vc;
    if (vc_create(&vc, special)) {
        close(fd);
        close(client_fd);
        return MX_ERR_INTERNAL;
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

    pty_window_size_t wsz = {
        .width = vc->columns,
        .height = vc->rows,
    };
    ioctl_pty_set_window_size(fd, &wsz);

    vc->fh.func = session_io_cb;

    *out = vc;
    *out_fd = client_fd;
    return MX_OK;
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

static mx_status_t new_vc_cb(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    mx_handle_t h;
    uint32_t dcount, hcount;
    if (mx_channel_read(ph->handle, 0, NULL, &h, 0, 1, &dcount, &hcount) < 0) {
        return MX_OK;
    }
    if (hcount != 1) {
        return MX_OK;
    }

    vc_t* vc;
    int fd;
    if (session_create(&vc, &fd, true, false) < 0) {
        mx_handle_close(h);
        return MX_OK;
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
    return MX_OK;
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

static mx_status_t input_cb(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    if (!(signals & MX_CHANNEL_READABLE)) {
        return MX_ERR_STOP;
    }

    // Buffer contains events { Opcode, Len, Name[Len] }
    // See magenta/device/vfs.h for more detail
    // extra byte is for temporary NUL
    uint8_t buf[VFS_WATCH_MSG_MAX + 1];
    uint32_t len;
    if (mx_channel_read(ph->handle, 0, buf, NULL, sizeof(buf) - 1, 0, &len, NULL) < 0) {
        return MX_ERR_STOP;
    }

    uint8_t* msg = buf;
    while (len >= 2) {
        uint8_t event = *msg++;
        uint8_t namelen = *msg++;
        if (len < (namelen + 2u)) {
            break;
        }
        // add temporary nul
        uint8_t tmp = msg[namelen];
        msg[namelen] = 0;
        input_dir_event(event, (char*) msg);
        msg[namelen] = tmp;
        msg += namelen;
        len -= (namelen + 2u);
    }
    return MX_OK;
}

static mx_status_t ownership_ph_cb(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
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

    return MX_OK;
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
    if (vc_create(&log_vc, false) != MX_OK) {
        return -1;
    }
    g_status_width = log_vc->columns;
    snprintf(log_vc->title, sizeof(log_vc->title), "debuglog");

    // Get our process koid so the log reader can
    // filter out our own debug messages from the log
    mx_info_handle_basic_t info;
    if (mx_object_get_info(mx_process_self(), MX_INFO_HANDLE_BASIC, &info,
                           sizeof(info), NULL, NULL) == MX_OK) {
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

    if ((input_dir_fd = open("/dev/class/input", O_DIRECTORY | O_RDONLY)) >= 0) {
        vfs_watch_dir_t wd;
        wd.mask = VFS_WATCH_MASK_ALL;
        wd.options = 0;
        if (mx_channel_create(0, &wd.channel, &input_ph.handle) == MX_OK) {
            if ((ioctl_vfs_watch_dir(input_dir_fd, &wd)) == MX_OK) {
                input_ph.waitfor = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
                input_ph.func = input_cb;
                port_wait(&port, &input_ph);
            } else {
                mx_handle_close(wd.channel);
                mx_handle_close(input_ph.handle);
                close(input_dir_fd);
            }
        } else {
            close(input_dir_fd);
        }
    }

    setenv("TERM", "xterm", 1);

    start_shell(keep_log ? false : true, cmd);
    start_shell(false, NULL);
    start_shell(false, NULL);

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
