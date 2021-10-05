// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/watcher.h>
#include <lib/stdcompat/span.h>
#include <zircon/types.h>

#include "fdio_unistd.h"

namespace fio = fuchsia_io;

__EXPORT
zx_status_t fdio_watch_directory(int dirfd, watchdir_func_t cb, zx_time_t deadline, void* cookie) {
  fdio_ptr io = fd_to_io(dirfd);
  if (io == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_handle_t handle;
  if (zx_status_t status = io->borrow_channel(&handle); status != ZX_OK) {
    return status;
  }
  fidl::UnownedClientEnd<fio::Directory> directory(handle);
  if (!directory.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx::status endpoints = fidl::CreateEndpoints<fio::DirectoryWatcher>();
  if (endpoints.is_error()) {
    return endpoints.status_value();
  }

  auto result = fidl::WireCall(directory)->Watch(fio::wire::kWatchMaskAll, 0,
                                                 endpoints->client.TakeChannel());
  if (zx_status_t status = result.status(); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = result->s; status != ZX_OK) {
    return status;
  }

  for (;;) {
    uint8_t bytes[fio::wire::kMaxBuf + 1];  // Extra byte for temporary null terminator.
    uint32_t num_bytes;
    zx_status_t status = endpoints->server.channel().read_etc(0, &bytes, nullptr, sizeof(bytes), 0,
                                                              &num_bytes, nullptr);
    if (status != ZX_OK) {
      if (status == ZX_ERR_SHOULD_WAIT) {
        status = endpoints->server.channel().wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                                      zx::time(deadline), nullptr);
        if (status != ZX_OK) {
          return status;
        }
        continue;
      }
      return status;
    }

    // Message Format: { OP, LEN, DATA[LEN] }
    cpp20::span span(bytes, num_bytes);
    auto it = span.begin();
    for (;;) {
      if (std::distance(it, span.end()) < 2) {
        break;
      }
      uint8_t event = *it++;
      uint8_t len = *it++;
      uint8_t* name = it;

      if (std::distance(it, span.end()) < len) {
        break;
      }
      it += len;

      switch (event) {
        case fio::wire::kWatchEventAdded:
        case fio::wire::kWatchEventExisting:
          event = WATCH_EVENT_ADD_FILE;
          break;
        case fio::wire::kWatchEventRemoved:
          event = WATCH_EVENT_REMOVE_FILE;
          break;
        case fio::wire::kWatchEventIdle:
          event = WATCH_EVENT_WAITING;
          break;
        default:
          // unsupported event
          continue;
      }

      // The callback expects a null-terminated string.
      uint8_t tmp = *it;
      *it = 0;
      status = cb(dirfd, event, reinterpret_cast<const char*>(name), cookie);
      *it = tmp;
      if (status != ZX_OK) {
        return status;
      }
    }
  }
}
