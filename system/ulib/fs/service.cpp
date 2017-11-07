// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>

namespace fs {

Service::Service(Connector connector)
    : connector_(fbl::move(connector)) {}

Service::~Service() = default;

zx_status_t Service::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    return ZX_OK;
}

zx_status_t Service::Getattr(vnattr_t* attr) {
    // TODO(ZX-1152): V_TYPE_FILE isn't right, we should use a type for services
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE;
    attr->nlink = 1;
    return ZX_OK;
}

zx_status_t Service::Serve(Vfs* vfs, zx::channel channel, uint32_t flags) {
    ZX_DEBUG_ASSERT(!(flags & ZX_FS_FLAG_DIRECTORY)); // checked by Open

    if (!connector_) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return connector_(fbl::move(channel));
}

} // namespace fs
