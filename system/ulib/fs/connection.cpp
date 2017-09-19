// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <fbl/ref_ptr.h>
#include <fs/connection.h>
#include <fs/vnode.h>

namespace fs {

Connection::Connection(fbl::RefPtr<fs::Vnode> vn, fs::Vfs* vfs, uint32_t flags)
    : vn_(fbl::move(vn)), vfs_(vfs), flags_(flags) {}

Connection::~Connection() {}

} // namespace fs
