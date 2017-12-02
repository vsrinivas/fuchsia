// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fbl/intrusive_double_list.h>
#include <fbl/unique_ptr.h>
#include <fs/connection.h>
#include <fs/vfs.h>

namespace fs {

// A specialization of |Vfs| which tears down all active connections when it
// is destroyed.
//
// Unlike |Vfs|, this class is NOT thread-safe and it must be used with a
// single-threaded asynchronous dispatcher.
class ManagedVfs : public Vfs {
public:
    ManagedVfs(async_t* async);
    ~ManagedVfs() override;

protected:
    void RegisterConnection(fbl::unique_ptr<Connection> connection) final;
    void UnregisterAndDestroyConnection(Connection* connection) final;

private:
    fbl::DoublyLinkedList<fbl::unique_ptr<Connection>> connections_;
};

} // namespace fs
