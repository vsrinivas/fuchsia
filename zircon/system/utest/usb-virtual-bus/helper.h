// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/loop.h>
#include <fuchsia/hardware/usb/peripheral/c/fidl.h>
#include <fbl/string.h>
#include <fbl/function.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/channel.h>

struct DispatchContext {
    bool state_changed;
    async::Loop* loop;
};

zx_status_t DispatchStateChange(void* ctx, fidl_txn_t* txn);

zx_status_t dispatch_wrapper(void* ctx, fidl_txn_t* txn, fidl_msg_t* msg, const void* ops);

template <typename F, typename... Args>
zx_status_t FidlCall(const F& function, Args... args) {
    zx_status_t status;
    zx_status_t status1;
    status = function(args..., &status1);
    if (status) {
        return status;
    }
    return status1;
}

zx_status_t AllocateString(const zx::channel& handle, const char* string, uint8_t* out);

zx_status_t WaitForAnyFile(int dirfd, int event, const char* name, void* cookie);

zx_status_t WaitForFile(int dirfd, int event, const char* fn, void* name);
