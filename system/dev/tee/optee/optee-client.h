// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/tee.h>
#include <fbl/intrusive_double_list.h>
#include <zircon/device/tee.h>

#include "optee-controller.h"

namespace optee {

class OpteeClient;
using OpteeClientBase = ddk::Device<OpteeClient, ddk::Closable, ddk::Ioctlable>;
using OpteeClientProtocol = ddk::TeeProtocol<OpteeClient>;

// The Optee driver allows for simultaneous access from different processes. The OpteeClient object
// is a distinct device instance for each client connection. This allows for per-instance state to
// be managed together. For example, if a client closes the device, OpteeClient can free all of the
// allocated shared memory buffers and sessions that were created by that client without interfering
// with other active clients.

class OpteeClient : public OpteeClientBase,
                    public OpteeClientProtocol,
                    public fbl::DoublyLinkedListable<OpteeClient*> {
public:
    explicit OpteeClient(OpteeController* controller)
        : OpteeClientBase(controller->zxdev()), controller_(controller) {}

    OpteeClient(const OpteeClient&) = delete;
    OpteeClient& operator=(const OpteeClient&) = delete;

    zx_status_t DdkClose(uint32_t flags);
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);

    // If the Controller is unbound, we need to notify all clients that the device is no longer
    // available. The Controller will invoke this function so that any subsequent calls on the
    // client will notify the caller that the peer has closed.
    void MarkForClosing() { needs_to_close_ = true; }

private:
    OpteeController* controller_;
    bool needs_to_close_ = false;
};

} // namespace optee
