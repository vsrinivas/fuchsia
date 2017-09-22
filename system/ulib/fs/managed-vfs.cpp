// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/managed-vfs.h>

#include <fbl/auto_lock.h>

namespace fs {

ManagedVfs::ManagedVfs(async_t* async)
    : Vfs(async) {}

ManagedVfs::~ManagedVfs() = default;

void ManagedVfs::RegisterConnection(fbl::unique_ptr<Connection> connection) {
    connections_.push_back(fbl::move(connection));
}

void ManagedVfs::UnregisterAndDestroyConnection(Connection* connection) {
    // We drop the result of |erase| on the floor, effectively destroying the connection.
    connections_.erase(*connection);
}

} // namespace fs
