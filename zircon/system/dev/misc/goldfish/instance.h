// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_
#define ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/goldfish/pipe.h>
#include <lib/zx/event.h>
#include <lib/zx/pmt.h>
#include <zircon/types.h>

namespace goldfish {

class Instance;
using InstanceType = ddk::Device<Instance, ddk::Readable, ddk::Writable,
                                 ddk::Messageable, ddk::Closable>;

// This class implements a pipe instance device. By opening the pipe device,
// an instance of this class will be created to service a new channel
// to the virtual device.
class Instance : public InstanceType {
public:
    explicit Instance(zx_device_t* parent);
    ~Instance();

    zx_status_t Bind();

    // FIDL interface
    zx_status_t FidlSetBufferSize(uint64_t size, fidl_txn_t* txn);
    zx_status_t FidlSetEvent(zx_handle_t event_handle);
    zx_status_t FidlGetBuffer(fidl_txn_t* txn);
    zx_status_t FidlRead(size_t count, zx_off_t offset, fidl_txn_t* txn);
    zx_status_t FidlWrite(size_t count, zx_off_t offset, fidl_txn_t* txn);

    // Device protocol implementation.
    zx_status_t DdkRead(void* buf, size_t len, zx_off_t off, size_t* actual);
    zx_status_t DdkWrite(const void* buf, size_t buf_len, zx_off_t off,
                         size_t* actual);
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    zx_status_t DdkClose(uint32_t flags);
    void DdkRelease();

private:
    static void OnSignal(void* ctx, int32_t flags);

    zx_status_t SetBufferSize(size_t size);
    zx_status_t Read(zx_paddr_t paddr, size_t count, size_t* actual);
    zx_status_t Write(zx_paddr_t paddr, size_t count, size_t* actual);
    zx_status_t Transfer(int32_t cmd, int32_t wake_cmd, zx_signals_t state_clr,
                         zx_signals_t dev_state_clr, zx_paddr_t paddr,
                         size_t count, size_t* actual);

    ddk::GoldfishPipeProtocolClient pipe_;
    int32_t id_ = 0;
    zx::bti bti_;
    ddk::IoBuffer cmd_buffer_;
    struct {
        zx::vmo vmo;
        zx::pmt pmt;
        size_t size;
        zx_paddr_t phys;
        zx::event event;
    } buffer_ = {};

    DISALLOW_COPY_ASSIGN_AND_MOVE(Instance);
};

} // namespace goldfish

#endif // ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_
