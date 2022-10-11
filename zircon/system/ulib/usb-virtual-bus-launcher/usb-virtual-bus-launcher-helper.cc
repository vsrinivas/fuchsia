// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/usb-virtual-bus-launcher-helper/usb-virtual-bus-launcher-helper.h"

#include <lib/fdio/watcher.h>
#include <lib/fit/function.h>

#include <fbl/string.h>

namespace usb_virtual_bus {

using Callback = fit::function<zx_status_t(int, const char*)>;
zx_status_t WatcherCallback(int dirfd, int event, const char* fn, void* cookie) {
  return (*reinterpret_cast<Callback*>(cookie))(event, fn);
}

zx_status_t WatchDirectory(int dirfd, Callback* callback) {
  return fdio_watch_directory(dirfd, WatcherCallback, ZX_TIME_INFINITE, callback);
}

__EXPORT
zx_status_t WaitForAnyFile(int dirfd, int event, const char* name, void* cookie) {
  if (std::string_view{name} == ".") {
    return ZX_OK;
  }
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  if (*name) {
    *reinterpret_cast<fbl::String*>(cookie) = fbl::String(name);
    return ZX_ERR_STOP;
  } else {
    return ZX_OK;
  }
}

__EXPORT
zx_status_t WaitForFile(int dirfd, int event, const char* fn, void* name) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  return strcmp(static_cast<const char*>(name), fn) ? ZX_OK : ZX_ERR_STOP;
}

}  // namespace usb_virtual_bus
