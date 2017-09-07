// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <threads.h>

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/syscalls/port.h>
#include <mx/event.h>
#include <mx/port.h>
#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/unique_ptr.h>
#include <mxio/dispatcher.h>
#include <fs/vfs.h>

#include "dispatcher.h"

namespace fs {

class Handler : public fbl::DoublyLinkedListable<fbl::unique_ptr<Handler>> {
public:
    Handler(mx::channel channel, vfs_dispatcher_cb_t cb, void* cookie) :
        channel_(fbl::move(channel)), cb_(cb), cookie_(cookie) {
    }
    ~Handler();

    mx_status_t SetAsyncCallback(const mx::port& dispatch_port);
    mx_status_t CancelAsyncCallback(const mx::port& dispatch_port);

    mx_status_t ExecuteCallback(mxio_dispatcher_cb_t dispatch_cb) {
        return dispatch_cb(channel_.get(), (void*) cb_, cookie_);
    }

    void ExecuteCloseCallback(mxio_dispatcher_cb_t dispatch_cb) {
        dispatch_cb(MX_HANDLE_INVALID, (void*) cb_, cookie_);
    }

    void Close();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Handler);

    mx::channel channel_;
    vfs_dispatcher_cb_t cb_;
    void* cookie_;
};

// VfsDispatcher is a dispatcher which uses a pool of threads to distribute
// requests to an underlying handlers concurrently.
class VfsDispatcher final : public fs::Dispatcher {
public:
    ~VfsDispatcher();

    static mx_status_t Create(mxio_dispatcher_cb_t cb, uint32_t pool_size,
                              fbl::unique_ptr<fs::VfsDispatcher>* out);
    void DisconnectHandler(Handler*, bool);
    void RunOnCurrentThread();
    mx_status_t AddVFSHandler(mx::channel channel, vfs_dispatcher_cb_t cb, void* iostate) final;
private:
    VfsDispatcher(mxio_dispatcher_cb_t cb, uint32_t pool_size);
    mx_status_t Start(const char* name);

    mxio_dispatcher_cb_t cb_;
    uint32_t pool_size_;
    fbl::Array<thrd_t> t_;
    mx::event shutdown_event_;

    mtx_t lock_;
    fbl::DoublyLinkedList<fbl::unique_ptr<Handler>> handlers_ __TA_GUARDED(lock_);
    uint32_t n_threads_;

    // NOTE: port_ intentionally declared after handlers_, so it
    // is shut down before the handlers are destroyed.
    mx::port port_;
};

} // namespace fs
