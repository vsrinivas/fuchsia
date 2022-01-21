// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-virtual-bus-helper.h"

#include <lib/fdio/watcher.h>

#include <fbl/string.h>

namespace usb_virtual_bus_helper {

zx_status_t WaitForAnyFile(int dirfd, int event, const char* name, void* cookie) {
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

zx_status_t WaitForFile(int dirfd, int event, const char* fn, void* name) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }
  return strcmp(static_cast<const char*>(name), fn) ? ZX_OK : ZX_ERR_STOP;
}

}  // namespace usb_virtual_bus_helper
