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

#include "devmgr.h"
#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/processargs.h>

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <runtime/mutex.h>

#include <system/listnode.h>

#define MXDEBUG 0

static const char* name = "devmgr";

iostate_t* create_iostate(mx_device_t* dev) {
    iostate_t* ios;
    if ((ios = calloc(1, sizeof(iostate_t))) == NULL) {
        return NULL;
    }
    ios->dev = dev;
    return ios;
}

mx_status_t __mx_rio_clone(mx_handle_t h, mx_handle_t* handles, uint32_t* types);

static mx_status_t __devmgr_get_handles(mx_device_t* dev, mx_handle_t* handles, uint32_t* ids) {
    iostate_t* newios;
    if (devmgr_is_remote) {
        name = "devhost";
    }

    // remote device: clone from remote devhost
    // TODO: timeout or handoff
    if (dev->flags & DEV_FLAG_REMOTE) {
        mx_status_t r = __mx_rio_clone(dev->remote, handles, ids);
        return r;
    }

    if ((newios = create_iostate(dev)) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t h0, h1;
    if ((h0 = _magenta_message_pipe_create(&h1)) < 0) {
        free(newios);
        return h0;
    }
    handles[0] = h0;
    ids[0] = MX_HND_TYPE_MXIO_REMOTE;

    mx_status_t r;
    void* cookie = NULL;
    if ((r = dev->ops->open(dev, 0, &cookie)) < 0) {
        printf("%s_get_handles(%p) open %d\n", name, dev, r);
        goto fail1;
    }

    if (dev->event > 0) {
        //TODO: read only?
        if ((handles[1] = _magenta_handle_duplicate(dev->event, MX_RIGHT_SAME_RIGHTS)) < 0) {
            r = handles[1];
            goto fail2;
        }
        ids[1] = MX_HND_TYPE_MXIO_REMOTE;
        r = 2;
    } else {
        r = 1;
    }

    char tmp[MX_DEVICE_NAME_MAX + 9];
    snprintf(tmp, sizeof(tmp), "device:%s", dev->name);
    track_iostate(newios, tmp);

    newios->cookie = cookie;
    mxio_dispatcher_add(devmgr_rio_dispatcher, h1, devmgr_rio_handler, newios);
    return r;

fail2:
    dev->ops->close(dev, cookie);
fail1:
    _magenta_handle_close(h0);
    _magenta_handle_close(h1);
    free(newios);
    return r;
}

static mxr_mutex_t rio_lock = MXR_MUTEX_INIT;

// This is called from both the vfs handler thread and console start thread
// and if not protected by rio_lock, they can step on each other when cloning
// remoted devices.
//
// TODO: eventually this should be integrated with core devmgr locking, but
//       that will require a bit more work.  This resolves the immediate issue.
mx_status_t devmgr_get_handles(mx_device_t* dev, mx_handle_t* handles, uint32_t* ids) {
    mxr_mutex_lock(&rio_lock);
    mx_status_t r = __devmgr_get_handles(dev, handles, ids);
    mxr_mutex_unlock(&rio_lock);
    return r;
}

mx_status_t devmgr_rio_handler(mx_rio_msg_t* msg, void* cookie) {
    iostate_t* ios = cookie;
    mx_device_t* dev = ios->dev;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    for (unsigned i = 0; i < msg->hcount; i++) {
        _magenta_handle_close(msg->handle[i]);
    }

    switch (MX_RIO_OP(msg->op)) {
    case MX_RIO_CLOSE:
        untrack_iostate(ios);
        free(ios);
        return NO_ERROR;
    case MX_RIO_CLONE: {
        xprintf("%s_rio_handler() clone dev %p name '%s'\n", name, dev, dev->name);
        if (ios->cookie) {
            // devices that have per-open context cannot
            // be cloned (at least for now)
            printf("%s_rio_handler() cannot clone dev %p name '%s'\n", name, dev, dev->name);
            return ERR_NOT_SUPPORTED;
        }
        uint32_t ids[VFS_MAX_HANDLES];
        mx_status_t r = devmgr_get_handles(dev, msg->handle, ids);
        if (r < 0) {
            return r;
        }

        msg->arg2.protocol = MXIO_PROTOCOL_REMOTE;
        msg->hcount = r;
        return NO_ERROR;
    }
    case MX_RIO_READ: {
        mx_status_t r = dev->ops->read(dev, msg->data, arg, ios->io_off, ios->cookie);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
            msg->datalen = r;
        }
        return r;
    }
    case MX_RIO_WRITE: {
        mx_status_t r = dev->ops->write(dev, msg->data, len, ios->io_off, ios->cookie);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
        }
        return r;
    }
    case MX_RIO_SEEK: {
        size_t end, n;
        end = dev->ops->get_size(dev, ios->cookie);
        switch (arg) {
        case SEEK_SET:
            if ((msg->arg2.off < 0) || ((size_t)msg->arg2.off > end)) {
                return ERR_INVALID_ARGS;
            }
            n = msg->arg2.off;
            break;
        case SEEK_CUR:
            // TODO: track seekability with flag, don't update off
            // at all on read/write if not seekable
            n = ios->io_off + msg->arg2.off;
            if (msg->arg2.off < 0) {
                // if negative seek
                if (n > ios->io_off) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < ios->io_off) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            }
            break;
        case SEEK_END:
            n = end + msg->arg2.off;
            if (msg->arg2.off <= 0) {
                // if negative or exact-end seek
                if (n > end) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            } else {
                if (n < end) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            }
            break;
        default:
            return ERR_INVALID_ARGS;
        }
        if (n > end) {
            // devices may not seek past the end
            return ERR_INVALID_ARGS;
        }
        ios->io_off = n;
        msg->arg2.off = ios->io_off;
        return NO_ERROR;
    }
    case MX_RIO_STAT: {
        msg->datalen = sizeof(vnattr_t);
        vnattr_t* attr = (void*) msg->data;
        memset(attr, 0, sizeof(vnattr_t));
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        attr->size = dev->ops->get_size(dev, ios->cookie);
        return msg->datalen;
    }
    case MX_RIO_IOCTL: {
        if (len > MXIO_IOCTL_MAX_INPUT || arg > (ssize_t)sizeof(msg->data)) {
            return ERR_INVALID_ARGS;
        }
        char in_buf[MXIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->data, len);
        mx_status_t r = dev->ops->ioctl(dev, msg->arg2.op, in_buf, len, msg->data, arg, ios->cookie);
        if (r >= 0) {
            msg->datalen = r;
            msg->arg2.off = ios->io_off;
        }
        return r;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

