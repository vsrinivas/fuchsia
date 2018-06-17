// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/fdio/private.h>
#include <lib/fdio/spawn.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/pty.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "shell.h"
#include "memalloc.h"
#include "nodes.h"
#include "exec.h"
#include "process.h"
#include "options.h"
#include "var.h"

static zx_status_t launch(const char* filename, const char* const* argv,
                          const char* const* envp, zx_handle_t* process,
                          zx_handle_t job, char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH]) {
    // cancel any ^c generated before running the command
    uint32_t events = 0;
    ioctl_pty_read_events(STDIN_FILENO, &events); // ignore any error

    // TODO(abarth): Including FDIO_SPAWN_CLONE_LDSVC doesn't fully make sense.
    // We should find a library loader that's appropriate for this program
    // rather than cloning the library loader used by the shell.
    uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_ENVIRON;
    return fdio_spawn_etc(job, flags, filename, argv, envp, 0, NULL, process, err_msg);
}

// Add all function definitions to our nodelist, so we can package them up for a
// subshell.
static void
addfuncdef(struct cmdentry *entry, void *token)
{
    if (entry->cmdtype == CMDFUNCTION) {
        struct nodelist **cmdlist = (struct nodelist **) token;
        struct nodelist *newnode = ckmalloc(sizeof(struct nodelist));
        newnode->next = *cmdlist;
        newnode->n = &entry->u.func->n;
        *cmdlist = newnode;
    }
}

zx_status_t process_subshell(union node* n, const char* const* envp,
                             zx_handle_t* process, zx_handle_t job,
                             int *fds, char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH])
{
    if (!orig_arg0)
        return ZX_ERR_NOT_FOUND;

    // TODO(abarth): Handle the redirects properly (i.e., implement
    // redirect(n->nredir.redirect) using launchpad);
    zx_handle_t ast_vmo = ZX_HANDLE_INVALID;

    // Create a node for our expression
    struct nodelist *nlist = ckmalloc(sizeof(struct nodelist));
    nlist->n = n;
    nlist->next = NULL;

    // Create nodes for all function definitions
    hashiter(addfuncdef, &nlist);

    // Encode the node list
    zx_status_t status = codec_encode(nlist, &ast_vmo);

    // Clean up
    while (nlist) {
        struct nodelist *next = nlist->next;
        ckfree(nlist);
        nlist = next;
    }

    if (status != ZX_OK)
        return status;

    // Construct an argv array
    int argc = 1 + shellparam.nparam;
    const char *argv[argc + 1];
    argv[0] = orig_arg0;
    size_t arg_ndx;
    for (arg_ndx = 0; arg_ndx < shellparam.nparam; arg_ndx++) {
        argv[arg_ndx + 1] = shellparam.p[arg_ndx];
    }
    argv[argc] = NULL;

    fdio_spawn_action_t actions[] = {
        {.action = FDIO_SPAWN_ACTION_CLONE_FD, .fd = {.local_fd = fds ? fds[0] : 0, .target_fd = 0}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD, .fd = {.local_fd = fds ? fds[1] : 1, .target_fd = 1}},
        {.action = FDIO_SPAWN_ACTION_CLONE_FD, .fd = {.local_fd = fds ? fds[2] : 2, .target_fd = 2}},
        {.action = FDIO_SPAWN_ACTION_ADD_HANDLE, .h = {.id = PA_HND(PA_USER0, 0), .handle = ast_vmo}},
    };

    // TODO(abarth): Including FDIO_SPAWN_CLONE_LDSVC doesn't fully make sense.
    // We should find a library loader that's appropriate for this program
    // rather than cloning the library loader used by the shell.
    uint32_t flags = FDIO_SPAWN_CLONE_JOB | FDIO_SPAWN_CLONE_LDSVC | FDIO_SPAWN_CLONE_NAMESPACE;
    return fdio_spawn_etc(job, flags, orig_arg0, argv, envp,
                          countof(actions), actions, process, err_msg);
}

int process_launch(const char* const* argv, const char* path, int index,
                   zx_handle_t* process, zx_handle_t job,
                   zx_status_t* status_out, char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH]) {
    zx_status_t status = ZX_OK;

    // All exported variables
    const char* const* envp = (const char* const*)environment();

    if (strchr(argv[0], '/') != NULL) {
        status = launch(argv[0], argv, envp, process, job, err_msg);
    } else {
        status = ZX_ERR_NOT_FOUND;
        const char* filename = NULL;
        while (status != ZX_OK && (filename = padvance(&path, argv[0])) != NULL) {
            if (--index < 0 && pathopt == NULL)
                status = launch(filename, argv, envp, process, job, err_msg);
            stunalloc(filename);
        }
    }

    *status_out = status;

    switch (status) {
    case ZX_OK:
        return 0;
    case ZX_ERR_ACCESS_DENIED:
        return 126;
    case ZX_ERR_NOT_FOUND:
        return 127;
    default:
        return 2;
    }
}

// TODO(ZX-972) When isatty correctly examines the fd, use that instead
int isapty(int fd) {
    fdio_t* io = __fdio_fd_to_io(fd);
    if (io == NULL) {
        errno = EBADF;
        return 0;
    }

    // if we can find the window size, it's a tty
    int ret;
    pty_window_size_t ws;
    int noread = ioctl_pty_get_window_size(fd, &ws);
    if (noread == sizeof(ws)) {
        ret = 1;
    } else {
        ret = 0;
        errno = ENOTTY;
    }

    __fdio_release(io);

    return ret;
}

/* Check for process termination (block if requested). When not blocking,
   returns ZX_ERR_TIMED_OUT if process hasn't exited yet.  */
int process_await_termination(zx_handle_t process, zx_handle_t job, bool blocking) {
    zx_time_t timeout = blocking ? ZX_TIME_INFINITE : 0;
    zx_status_t status;
    zx_wait_item_t wait_objects[2];
    fdio_t* tty = (isapty(STDIN_FILENO) ? __fdio_fd_to_io(STDIN_FILENO) : NULL);

    bool running = true;
    while (running) {
        int no_wait_obj = 0;
        int tty_wait_obj = -1;
        wait_objects[no_wait_obj].handle = process;
        wait_objects[no_wait_obj].waitfor = ZX_TASK_TERMINATED;
        wait_objects[no_wait_obj].pending = 0;
        no_wait_obj++;

        if (tty) {
            wait_objects[no_wait_obj].pending = 0;
            __fdio_wait_begin(tty, POLLPRI, &wait_objects[no_wait_obj].handle, &wait_objects[no_wait_obj].waitfor);
            tty_wait_obj = no_wait_obj;
            no_wait_obj++;
        }

        status = zx_object_wait_many(wait_objects, no_wait_obj, timeout);

        uint32_t interrupt_event = 0;
        if (tty) {
            __fdio_wait_end(tty, wait_objects[tty_wait_obj].pending, &interrupt_event);
        }

        if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
            running = false;
        } else if (wait_objects[0].pending & ZX_TASK_TERMINATED) {
            // process ended normally
            status = ZX_OK;
            running = false;
        } else if (tty && (interrupt_event & POLLPRI)) {
            // interrupted - kill process
            uint32_t events = 0;
            int noread = ioctl_pty_read_events(STDIN_FILENO, &events);
            if (noread == sizeof(events) && (events & PTY_EVENT_INTERRUPT)) {
                // process belongs to job, so killing the job kills the process
                status = zx_task_kill(job);
                // If the kill failed status is going to be ZX_ERR_ACCESS_DENIED
                // which is not likely since the user started this process
                if (status == ZX_OK) {
                    status = ZX_ERR_CANCELED;
                }
                running = false;
            }
        }
    }
    if (tty)
        __fdio_release(tty);

    if (status != ZX_OK)
        return status;

    zx_info_process_t proc_info;
    status = zx_object_get_info(process, ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL);
    if (status != ZX_OK)
        return status;

    return proc_info.return_code;
}
