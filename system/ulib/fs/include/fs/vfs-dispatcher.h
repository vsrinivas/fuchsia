// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <threads.h>

#include <magenta/types.h>
#include <magenta/syscalls/port.h>

#include <mxtl/array.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>

#include <mxio/dispatcher.h>
#include <fs/vfs.h>

namespace fs {

class Handler : public mxtl::DoublyLinkedListable<Handler*> {
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Handler);

    mx_handle_t h_;
    void* cb_;
    void* cookie_;

public:
    Handler(mx_handle_t h, void* cb, void* cookie) :
        h_(h), cb_(cb), cookie_(cookie) {
    }
    ~Handler();

    mx_status_t SetAsyncCallback(mx_handle_t dispatch_ioport);

    mx_status_t CancelAsyncCallback(mx_handle_t dispatch_ioport);

    mx_status_t ExecuteCallback(mxio_dispatcher_cb_t dispatch_cb) {
        return dispatch_cb(h_, cb_, cookie_);
    }

    void ExecuteCloseCallback(mxio_dispatcher_cb_t dispatch_cb) {
        (void)dispatch_cb(0, cb_, cookie_);
    }

    void Close();
};

class VfsDispatcher : public mxtl::RefCounted<VfsDispatcher> {
private:
    mx_handle_t ioport_;
    mxio_dispatcher_cb_t cb_;
    uint32_t pool_size_;
    mxtl::Array<thrd_t> t_;
    mx_handle_t shutdown_event_;

    mtx_t lock_;
    mxtl::DoublyLinkedList<Handler*> handlers_;
    uint32_t n_threads_;

 public:
    ~VfsDispatcher();

    mx_status_t Create(mxio_dispatcher_cb_t cb, uint32_t pool_size);
    mx_status_t Add(mx_handle_t h, void* cb, void* cookie);
    mx_status_t Start(const char* name);
    int Loop();
    void DisconnectHandler(Handler*, bool);
};

}
