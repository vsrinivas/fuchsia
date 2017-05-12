// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <threads.h>

#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/util.h>

#include "private.h"
#include "unistd.h"

// TODO: should use a system default
#define MAX_WAIT_EVENTS 1024

typedef struct mxio_epoll_cookie {
    list_node_t node;
    mxio_t* io;
    struct epoll_event ep_event;
    int fd;
} mxio_epoll_cookie_t;

typedef struct mxio_epoll {
    mxio_t io;
    mx_handle_t h;
    mtx_t cookies_lock;
    list_node_t cookies;
} mxio_epoll_t;

static void mxio_epoll_cookie_add(mxio_epoll_t* epio,
                                  mxio_epoll_cookie_t* cookie) {
    mtx_lock(&epio->cookies_lock);
    list_add_head(&epio->cookies, &cookie->node);
    mtx_unlock(&epio->cookies_lock);
}

static mxio_epoll_cookie_t* mxio_epoll_cookie_get(mxio_epoll_t* epio, int fd,
                                                  bool remove) {
    mxio_epoll_cookie_t* cookie = NULL;
    mxio_epoll_cookie_t* entry = NULL;
    mtx_lock(&epio->cookies_lock);
    list_for_every_entry(&epio->cookies, entry, mxio_epoll_cookie_t, node) {
        // doesn't have to be _safe as we break immediately after deletion
        if (entry->fd == fd) {
            if (remove) {
                list_delete(&entry->node);
            }
            cookie = entry;
            break;
        }
    }
    mtx_unlock(&epio->cookies_lock);
    return cookie;
}

static mxio_epoll_cookie_t* mxio_epoll_cookie_find(mxio_epoll_t* epio, int fd) {
    return mxio_epoll_cookie_get(epio, fd, false);
}

static mxio_epoll_cookie_t* mxio_epoll_cookie_remove(mxio_epoll_t* epio, int fd) {
    return mxio_epoll_cookie_get(epio, fd, true);
}

static mx_status_t mxio_epoll_close(mxio_t* io) {
    mxio_epoll_t* epio = (mxio_epoll_t*)io;
    mx_handle_t h = epio->h;
    epio->h = MX_HANDLE_INVALID;
    mx_handle_close(h);

    mxio_epoll_cookie_t* cookie;
    mtx_lock(&epio->cookies_lock);
    while ((cookie = list_remove_head_type(&epio->cookies, mxio_epoll_cookie_t,
                                           node))) {
        mxio_release(cookie->io);
        free(cookie);
    }
    mtx_unlock(&epio->cookies_lock);
    return NO_ERROR;
}

static mxio_ops_t mxio_epoll_ops = {
    .read = mxio_default_read,
    .write = mxio_default_write,
    .recvmsg = mxio_default_recvmsg,
    .sendmsg = mxio_default_sendmsg,
    .seek = mxio_default_seek,
    .misc = mxio_default_misc,
    .close = mxio_epoll_close,
    .open = mxio_default_open,
    .clone = mxio_default_clone,
    .ioctl = mxio_default_ioctl,
    .wait_begin = mxio_default_wait_begin,
    .wait_end = mxio_default_wait_end,
    .posix_ioctl = mxio_default_posix_ioctl,
};

// Takes ownership of h.
static mxio_t* mxio_epoll_create(mx_handle_t h) {
    mxio_epoll_t* epio = calloc(1, sizeof(*epio));
    if (epio == NULL) {
        mx_handle_close(h);
        return NULL;
    }
    epio->io.ops = &mxio_epoll_ops;
    epio->io.magic = MXIO_MAGIC;
    atomic_init(&epio->io.refcount, 1);
    epio->io.flags |= MXIO_FLAG_EPOLL;
    epio->h = h;
    mtx_init(&epio->cookies_lock, mtx_plain);
    list_initialize(&epio->cookies);
    return &epio->io;
}

mx_status_t mxio_epoll(mxio_t** out) {
    mx_handle_t h;
    mx_status_t status;
    if ((status = mx_waitset_create(0, &h)) < 0) {
        return status;
    }
    mxio_t* io;
    if ((io = mxio_epoll_create(h)) == NULL) {
        return ERR_NO_MEMORY;
    }
    *out = io;
    return NO_ERROR;
}

int epoll_create1(int flags) {
    mxio_t* io;
    mx_status_t r;
    if ((r = mxio_epoll(&io)) < 0) {
        return ERROR(r);
    }
    int fd;
    if ((fd = mxio_bind_to_fd(io, -1, 0)) < 0) {
        io->ops->close(io);
        mxio_release(io);
        return ERRNO(EMFILE);
    }
    return fd;
}

int epoll_create(int size) {
    return epoll_create1(0);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event* ep_event) {
    mxio_t* io = NULL;
    mxio_epoll_t* epio = NULL;
    mx_status_t r = NO_ERROR;

    // TODO: cascaded epoll is not implemented, yet.

    if ((op != EPOLL_CTL_DEL && ep_event == NULL) || epfd == fd) {
        r = ERR_INVALID_ARGS;
        goto fail_no_epio;
    }
    if ((epio = (mxio_epoll_t*)fd_to_io(epfd)) == NULL) {
        r = ERR_BAD_HANDLE;
        goto fail_no_epio;
    }
    if (!(epio->io.flags & MXIO_FLAG_EPOLL)) {
        r = ERR_INVALID_ARGS;
        goto fail_no_io;
    }

    if ((io = fd_to_io(fd)) == NULL) {
        r = ERR_BAD_HANDLE;
        goto fail_no_io;
    }

    mxio_epoll_cookie_t* cookie;
    switch (op) {
    case EPOLL_CTL_ADD:
        if (mxio_epoll_cookie_find(epio, fd) != NULL)  {
            r = ERR_ALREADY_EXISTS;
            goto end;
        }
        // create a new cookie
        cookie = calloc(1, sizeof(mxio_epoll_cookie_t));
        if (cookie == NULL) {
            r = ERR_NO_MEMORY;
            goto end;
        }
        mxio_acquire(io);
        cookie->io = io;
        cookie->fd = fd;
        break;
    case EPOLL_CTL_MOD:
    case EPOLL_CTL_DEL:
        // or retrieve an existing cookie and remove the current epoll event
        cookie = mxio_epoll_cookie_remove(epio, fd);
        if (cookie == NULL) {
            r = ERR_NOT_FOUND;
            goto end;
        }
        if ((r = mx_waitset_remove(epio->h, (uint64_t)(uintptr_t)cookie)) < 0) {
            mxio_epoll_cookie_add(epio, cookie);
            goto end;
        }
        break;
    default:
        r = ERR_INVALID_ARGS;
        goto end;
    }

    if (op == EPOLL_CTL_DEL) {
        // free the cookie
        mxio_release(cookie->io);
        free(cookie);
    } else {
        // or add a new epoll event and put the cookie into the list
        mx_handle_t h = MX_HANDLE_INVALID;
        mx_signals_t signals = 0;
        io->ops->wait_begin(io, ep_event->events, &h, &signals);
        if (h == MX_HANDLE_INVALID) {
            // wait operation is not applicable to the handle
            r = ERR_INVALID_ARGS;
            mxio_release(cookie->io);
            free(cookie);
            goto end;
        }

        cookie->ep_event = *ep_event;
        if ((r = mx_waitset_add(epio->h, (uint64_t)(uintptr_t)cookie, h, signals)) < 0) {
            mxio_release(cookie->io);
            free(cookie);
            goto end;
        }
        mxio_epoll_cookie_add(epio, cookie);
    }

 end:
    mxio_release(io);
 fail_no_io:
    mxio_release(&epio->io);
 fail_no_epio:
    return STATUS(r);
}

int epoll_wait(int epfd, struct epoll_event* ep_events, int maxevents, int timeout) {
    if (maxevents <= 0 || timeout < -1) {
        return ERRNO(EINVAL);
    }
    if (ep_events == NULL) {
        return ERRNO(EFAULT);
    }
    if (maxevents > MAX_WAIT_EVENTS) {
        return ERRNO(EINVAL);
    }
    mxio_t* io;
    if ((io = fd_to_io(epfd)) == NULL) {
        return ERROR(ERR_BAD_HANDLE);
    }
    if (!(io->flags & MXIO_FLAG_EPOLL)) {
        mxio_release(io);
        return ERRNO(EINVAL);
    }
    mxio_epoll_t* epio = (mxio_epoll_t*)io;

    mx_status_t r;
    uint32_t num_results = maxevents;
    mx_waitset_result_t results[maxevents];
    mx_time_t tmo = (timeout >= 0) ? mx_deadline_after(MX_MSEC(timeout)) : MX_TIME_INFINITE;

    if ((r = mx_waitset_wait(epio->h, tmo, results, &num_results)) < 0) {
        mxio_release(io);
        return (r == ERR_TIMED_OUT) ? 0 : ERROR(r);
    }

    for (uint32_t i = 0; i < num_results; i++) {
        mxio_epoll_cookie_t* cookie = (mxio_epoll_cookie_t*)(uintptr_t)results[i].cookie;
        mxio_t* io = cookie->io;
        uint32_t events;

        io->ops->wait_end(io, results[i].observed, &events);
        // mask unrequested events except HUP/ERR
        ep_events[i].events = events & (cookie->ep_event.events | EPOLLHUP | EPOLLERR);

        ep_events[i].data = cookie->ep_event.data;
    }
    mxio_release(io);
    return (int)num_results;
}

int epoll_pwait(int epfd, struct epoll_event* events, int maxevents, int timeout, const sigset_t* sigmask) {
    return ERROR(ERR_NOT_SUPPORTED);
}
