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

    // IOCTLs
    zx_status_t OpenSession(const tee_ioctl_session_request_t* session_request,
                            tee_ioctl_session_t* out_session,
                            size_t* out_actual);

private:
    zx_status_t ConvertIoctlParamsToOpteeParams(const tee_ioctl_param_t* params,
                                                size_t num_params,
                                                fbl::Array<MessageParam>* out_optee_params);
    zx_status_t AllocateSharedMemory(size_t size, SharedMemory** out_shared_memory);
    zx_status_t HandleRpc(const RpcFunctionArgs& args, RpcFunctionResult* out_result);
    zx_status_t HandleRpcAllocateMemory(const RpcFunctionAllocateMemoryArgs& args,
                                        RpcFunctionAllocateMemoryResult* out_result);
    zx_status_t HandleRpcFreeMemory(const RpcFunctionFreeMemoryArgs& args,
                                    RpcFunctionFreeMemoryResult* out_result);

    OpteeController* controller_;
    bool needs_to_close_ = false;
    fbl::DoublyLinkedList<fbl::unique_ptr<SharedMemory>> allocated_shared_memory_;
};

} // namespace optee
