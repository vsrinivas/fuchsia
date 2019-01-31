// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <fbl/macros.h>
#include <lib/zx/channel.h>

#include "vnode.h"

namespace fs {

// A node which binds a channel to a service implementation when opened.
//
// This class is thread-safe.
class Service : public Vnode {
public:
    // Handler called to bind the provided channel to an implementation
    // of the service.
    using Connector = fbl::Function<zx_status_t(zx::channel channel)>;

    // Creates a service with the specified connector.
    //
    // If the |connector| is null, then incoming connection requests will be dropped.
    Service(Connector connector);

    // Destroys the services and releases its connector.
    ~Service() override;

    // |Vnode| implementation:
    zx_status_t ValidateFlags(uint32_t flags) final;
    zx_status_t Getattr(vnattr_t* a) final;
    zx_status_t Serve(fs::Vfs* vfs, zx::channel channel, uint32_t flags) final;

private:
    Connector connector_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Service);
};

} // namespace fs
