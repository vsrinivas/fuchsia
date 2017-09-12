// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fs/vfs.h>
#include <zx/channel.h>

namespace fs {

// RemoteContainer adds support for mounting remote handles on nodes.
class RemoteContainer {
public:
    constexpr RemoteContainer() {};
    bool IsRemote() const;
    zx::channel DetachRemote(uint32_t &flags_);
    // Access the remote handle if it's ready -- otherwise, return an error.
    zx_handle_t WaitForRemote(uint32_t &flags_);
    zx_handle_t GetRemote() const;
    void SetRemote(zx::channel remote);
private:
    zx::channel remote_;
};

}
