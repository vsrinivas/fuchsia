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
#include <mxtl/array.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>
#include <mxtl/unique_ptr.h>
#include <mxio/dispatcher.h>
#include <fs/vfs.h>

#include "dispatcher.h"

namespace fs {

class Handler : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<Handler>> {
public:
    Handler(mx_handle_t h, void* cb, void* cookie) :
        h_(h), cb_(cb), cookie_(cookie) {
    }
    ~Handler();

    mx_status_t SetAsyncCallback(const mx::port& dispatch_ioport);
    mx_status_t CancelAsyncCallback(const mx::port& dispatch_ioport);

    mx_status_t ExecuteCallback(mxio_dispatcher_cb_t dispatch_cb) {
        return dispatch_cb(h_.get(), cb_, cookie_);
    }

    void ExecuteCloseCallback(mxio_dispatcher_cb_t dispatch_cb) {
        dispatch_cb(MX_HANDLE_INVALID, cb_, cookie_);
    }

    void Close();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Handler);

    mx::handle h_;
    void* cb_;
    void* cookie_;
};

// VfsDispatcher is a dispatcher which uses a pool of threads to distribute
// requests to an underlying handlers concurrently.
class VfsDispatcher final : public fs::Dispatcher {
public:
    ~VfsDispatcher();

    static mx_status_t Create(mxio_dispatcher_cb_t cb, uint32_t pool_size,
                              mxtl::unique_ptr<fs::Dispatcher>* out);
    void DisconnectHandler(Handler*, bool);
    int Loop();
private:
    VfsDispatcher(mxio_dispatcher_cb_t cb, uint32_t pool_size);
    mx_status_t AddVFSHandler(mx_handle_t h, void* cb, void* iostate) final;
    mx_status_t Start(const char* name);

    mxio_dispatcher_cb_t cb_;
    uint32_t pool_size_;
    mxtl::Array<thrd_t> t_;
    mx::event shutdown_event_;

    mtx_t lock_;
    mxtl::DoublyLinkedList<mxtl::unique_ptr<Handler>> handlers_ __TA_GUARDED(lock_);
    uint32_t n_threads_;

    // NOTE: ioport_ intentionally declared after handlers_, so it
    // is shut down before the handlers are destroyed.
    mx::port ioport_;
};

} // namespace fs
