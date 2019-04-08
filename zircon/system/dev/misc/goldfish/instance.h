// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_
#define ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/goldfish/pipe.h>
#include <zircon/types.h>

namespace goldfish {

class Instance;
using InstanceType =
    ddk::Device<Instance, ddk::Readable, ddk::Writable, ddk::Closable>;

// This class implements a pipe instance device. By opening the pipe device,
// an instance of this class will be created to service a new channel
// to the virtual device.
class Instance : public InstanceType {
public:
    explicit Instance(zx_device_t* parent);
    ~Instance();

    zx_status_t Bind();

    // Device protocol implementation.
    zx_status_t DdkRead(void* buf, size_t len, zx_off_t off, size_t* actual);
    zx_status_t DdkWrite(const void* buf, size_t buf_len, zx_off_t off,
                         size_t* actual);
    zx_status_t DdkClose(uint32_t flags);
    void DdkRelease();

private:
    static void OnSignal(void* ctx, int32_t flags);

    zx_status_t Transfer(int32_t cmd, int32_t wake_cmd,
                         zx_signals_t dev_state_clr, zx_paddr_t paddr,
                         size_t count, size_t* actual);

    ddk::GoldfishPipeProtocolClient pipe_;
    int32_t id_ = 0;
    zx::bti bti_;
    ddk::IoBuffer cmd_buffer_;
    ddk::IoBuffer io_buffer_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Instance);
};

} // namespace goldfish

#endif // ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_
