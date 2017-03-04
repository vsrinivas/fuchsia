// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>
#include <mxio/socket.h>

#include <magenta/device/devmgr.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "apps/netstack/dispatcher.h"
#include "apps/netstack/handle_watcher.h"
#include "apps/netstack/iostate.h"
#include "apps/netstack/request_queue.h"
#include "apps/netstack/trace.h"

static mxio_dispatcher_t* remoteio_dispatcher;

static mxrio_msg_t* msg_dup(mxrio_msg_t* msg) {
  // msg->datalen : the data size in the request
  // msg->arg : the max data size in the reply
  size_t len =
      MXRIO_HDR_SZ + (((int)msg->datalen > msg->arg) ? msg->datalen : msg->arg);
  mxrio_msg_t* msg_copy = calloc(1, len);
  debug_alloc("msg_dup %p\n", msg_copy);
  assert(msg_copy);
  return memcpy(msg_copy, msg, len);
}

mx_status_t rio_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
  iostate_t* ios = cookie;

  if (rh == MX_HANDLE_INVALID) {
    return ERR_INVALID_ARGS;
  }

  if (msg->hcount) {
    if ((msg->hcount > 1) || (MXRIO_OP(msg->op) != MXRIO_OPEN)) {
      // only OPEN may acccept a handle
      for (unsigned i = 0; i < msg->hcount; i++) {
        mx_handle_close(msg->handle[i]);
      }
      return ERR_INVALID_ARGS;
    }
  }

  vdebug("rio_handler: op=%s, sockfd=%d, len=%u, arg=%d\n",
        getopname(MXRIO_OP(msg->op)), ios ? ios->sockfd : -999, msg->datalen,
        msg->arg);

  switch (MXRIO_OP(msg->op)) {
    case MXRIO_CLOSE:
      debug("rio_handler: iostate_release: %p\n", ios);
      iostate_release(ios);
      return NO_ERROR;
  }

  if (shared_queue_pack_and_put(MXRIO_OP(msg->op), rh, msg_dup(msg), ios) < 0) {
    debug("rio_handler: shared_queue_pack_and_put failed\n");
    for (unsigned i = 0; i < msg->hcount; i++) {
      mx_handle_close(msg->handle[i]);
    }
    return ERR_IO;
  }
  return ERR_DISPATCHER_INDIRECT;
}

mx_status_t dispatcher_add(mx_handle_t h, iostate_t* ios) {
  return mxio_dispatcher_add(remoteio_dispatcher, h, rio_handler, ios);
}

static mx_handle_t devmgr_connect(const char* where) {
  int fd;
  if ((fd = open(where, O_DIRECTORY | O_RDWR)) < 0) {
    error("netstack: cannot open %s\n", where);
    return -1;
  }

  // Create a channel, and connect one end of that channel to a vnode.
  mx_handle_t h, vnode_handle;
  mx_status_t status;
  if ((status = mx_channel_create(0, &h, &vnode_handle)) != NO_ERROR) {
    close(fd);
    return status;
  } else if ((status = ioctl_devmgr_mount_fs(fd, &vnode_handle)) != NO_ERROR) {
    mx_handle_close(h);
    mx_handle_close(vnode_handle);
    close(fd);
    error("netstack: failed to attach to %s\n", where);
    return status;
  }
  close(fd);
  return h;
}

mx_status_t dispatcher(void) {
  mx_status_t r;
  if ((r = mxio_dispatcher_create(&remoteio_dispatcher, mxrio_handler)) < 0) {
    return r;
  }
  mx_handle_t h;
  if ((h = devmgr_connect(MXRIO_SOCKET_ROOT)) < 0) {
    return h;
  }
  // Inform upstream that we are ready to serve.
  if ((r = mx_object_signal_peer(h, 0, MX_USER_SIGNAL_0)) != NO_ERROR) {
    return r;
  }
  if ((r = mxio_dispatcher_add(remoteio_dispatcher, h, rio_handler, NULL)) <
      0) {
    return r;
  }
  debug("netstack: run remoteio_dispatcher\n");
  mxio_dispatcher_run(remoteio_dispatcher);  // never return

  // TODO: destroy dispatcher
  return NO_ERROR;
}
