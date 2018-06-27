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

#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <port/port.h>
#include <zircon/device/pty.h>
#include <zircon/device/vfs.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/object.h>

#include "vc.h"

port_t port;
static port_handler_t log_ph;
static port_handler_t new_vc_ph;
static port_handler_t input_ph;

static int input_dir_fd;

static vc_t* log_vc;
static zx_koid_t proc_koid;

static zx_status_t log_reader_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    char buf[ZX_LOG_RECORD_MAX];
    zx_log_record_t* rec = (zx_log_record_t*)buf;
    zx_status_t status;
    for (;;) {
        if ((status = zx_debuglog_read(ph->handle, 0, rec, ZX_LOG_RECORD_MAX)) < 0) {
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
    const char* argv[] = { "/boot/bin/sh", nullptr, nullptr, nullptr };

    if (cmd) {
        argv[1] = "-c";
        argv[2] = cmd;
    }

    fdio_spawn_action_t actions[2] = {};
    actions[0].action = FDIO_SPAWN_ACTION_SET_NAME;
    actions[0].name.data = "vc:sh";
    actions[1].action = FDIO_SPAWN_ACTION_TRANSFER_FD;
    actions[1].fd = {.local_fd = fd, .target_fd = FDIO_FLAG_USE_FOR_STDIO};

    uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;

    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], argv,
                                        nullptr, countof(actions), actions,
                                        &vc->proc, err_msg);
    if (status != ZX_OK) {
        printf("vc: cannot spawn shell: %s: %d (%s)\n", err_msg, status,
               zx_status_get_string(status));
    }
    return status;
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

            if (launch_shell(vc, fd, NULL) < 0) {
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
    if (r != 2) {
        zx_handle_close_many(handles, r);
        session_destroy(vc);
    } else if (zx_channel_write(h, 0, types, 2 * sizeof(uint32_t), handles, 2) != ZX_OK) {
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
        event_handler(event, (char*)msg);
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

void set_log_listener_active(bool active) {
    if (active) {
        port_wait_repeating(&port, &log_ph);
    } else {
        port_cancel(&port, &log_ph);
    }
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
    int shells = 0;
    while (argc > 1) {
        if (!strcmp(argv[1], "--run")) {
            if (argc > 2) {
                argc--;
                argv++;
                cmd = argv[1];
                if (shells < 1)
                    shells = 1;
                printf("CMD: %s\n", cmd);
            }
        } else if (!strcmp(argv[1], "--shells")) {
            if (argc > 2) {
                argc--;
                argv++;
                shells = atoi(argv[1]);
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

    if ((new_vc_ph.handle = zx_take_startup_handle(PA_HND(PA_USER0, 0))) != ZX_HANDLE_INVALID) {
        new_vc_ph.func = new_vc_cb;
        new_vc_ph.waitfor = ZX_CHANNEL_READABLE;
        port_wait(&port, &new_vc_ph);
    }

    setup_dir_watcher("/dev/class/input", input_cb, &input_ph, &input_dir_fd);

    if (!vc_display_init()) {
        return -1;
    }

    setenv("TERM", "xterm", 1);

    for (int i = 0; i < shells; ++i) {
        if (i == 0)
            start_shell(!keep_log, cmd);
        else
            start_shell(false, NULL);
    }

    zx_status_t r = port_dispatch(&port, ZX_TIME_INFINITE, false);
    printf("vc: port failure: %d\n", r);
    return -1;
}
