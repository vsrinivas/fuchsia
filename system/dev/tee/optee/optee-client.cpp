// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-client.h"

namespace optee {

zx_status_t OpteeClient::DdkClose(uint32_t flags) {
    controller_->RemoveClient(this);
    return ZX_OK;
}

void OpteeClient::DdkRelease() {
    // devmgr has given up ownership, so we must clean ourself up.
    delete this;
}

zx_status_t OpteeClient::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                                  size_t out_len, size_t* out_actual) {
    if (needs_to_close_) {
        return ZX_ERR_PEER_CLOSED;
    }

    switch (op) {
    case IOCTL_TEE_GET_DESCRIPTION: {
        if ((out_buf == nullptr) || (out_len != sizeof(tee_ioctl_description_t)) ||
            (out_actual == nullptr)) {
            return ZX_ERR_INVALID_ARGS;
        }

        return controller_->GetDescription(reinterpret_cast<tee_ioctl_description_t*>(out_buf),
                                           out_actual);
    }
    }

    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace optee
