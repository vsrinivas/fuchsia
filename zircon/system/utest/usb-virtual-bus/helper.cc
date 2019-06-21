// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <fbl/string.h>
#include <fbl/function.h>
#include <lib/fdio/watcher.h>

zx_status_t DispatchStateChange(void* ctx, fidl_txn_t* txn) {
    DispatchContext* context = reinterpret_cast<DispatchContext*>(ctx);
    context->state_changed = true;
    context->loop->Quit();
    return ZX_ERR_CANCELED;
}

zx_status_t dispatch_wrapper(void* ctx, fidl_txn_t* txn, fidl_msg_t* msg, const void* ops) {
    return fuchsia_hardware_usb_peripheral_Events_dispatch(
        ctx, txn, msg, reinterpret_cast<const fuchsia_hardware_usb_peripheral_Events_ops_t*>(ops));
}

zx_status_t AllocateString(const zx::channel& handle, const char* string, uint8_t* out) {
    zx_status_t status1;
    zx_status_t status = fuchsia_hardware_usb_peripheral_DeviceAllocStringDesc(
        handle.get(), string, strlen(string), &status1, out);
    if (status) {
        return status;
    }
    return status1;
}

using Callback = fbl::Function<zx_status_t(int, const char*)>;
zx_status_t WatcherCallback(int dirfd, int event, const char* fn, void* cookie) {
    return (*reinterpret_cast<Callback*>(cookie))(event, fn);
}

zx_status_t WatchDirectory(int dirfd, Callback* callback) {
    return fdio_watch_directory(dirfd, WatcherCallback, ZX_TIME_INFINITE, callback);
}

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
