// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_CONTROL_CONTROL_DEVICE_H_
#define ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_CONTROL_CONTROL_DEVICE_H_

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/goldfish/control.h>
#include <ddktl/protocol/goldfish/pipe.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <fuchsia/hardware/goldfish/control/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include <map>

namespace goldfish {

class Control;
using ControlType = ddk::Device<Control, ddk::Unbindable, ddk::Messageable,
                                ddk::GetProtocolable>;

class Control
    : public ControlType,
      public ddk::GoldfishControlProtocol<Control, ddk::base_protocol> {
public:
    static zx_status_t Create(void* ctx, zx_device_t* parent);

    explicit Control(zx_device_t* parent);
    ~Control();

    zx_status_t Bind();

    void RegisterColorBuffer(zx_koid_t koid);
    void FreeColorBuffer(zx_koid_t koid);

    zx_status_t FidlCreateColorBuffer(zx_handle_t vmo_handle, uint32_t width,
                                      uint32_t height, uint32_t format,
                                      fidl_txn_t* txn);
    zx_status_t FidlGetColorBuffer(zx_handle_t vmo_handle, fidl_txn_t* txn);

    // Device protocol implementation.
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);
    zx_status_t GoldfishControlGetColorBuffer(zx::vmo vmo, uint32_t* out_id);

private:
    static void OnSignal(void* ctx, int32_t flags);
    void OnReadable();

    int32_t WriteLocked(uint32_t cmd_size, int32_t* consumed_size)
        TA_REQ(lock_);
    void WriteLocked(uint32_t cmd_size) TA_REQ(lock_);
    zx_status_t ReadResultLocked(uint32_t* result) TA_REQ(lock_);
    zx_status_t ExecuteCommandLocked(uint32_t cmd_size, uint32_t* result)
        TA_REQ(lock_);
    zx_status_t CreateColorBufferLocked(uint32_t width, uint32_t height,
                                        uint32_t format, uint32_t* id)
        TA_REQ(lock_);
    void CloseColorBufferLocked(uint32_t id) TA_REQ(lock_);
    zx_status_t SetColorBufferVulkanModeLocked(uint32_t id, uint32_t mode,
                                               uint32_t* result) TA_REQ(lock_);

    fbl::Mutex lock_;
    fbl::ConditionVariable readable_cvar_;
    ddk::GoldfishPipeProtocolClient pipe_;
    ddk::GoldfishControlProtocolClient control_;
    int32_t id_ = 0;
    zx::bti bti_ TA_GUARDED(lock_);
    ddk::IoBuffer cmd_buffer_ TA_GUARDED(lock_);
    ddk::IoBuffer io_buffer_ TA_GUARDED(lock_);
    // TODO(TC-383): This should be std::unordered_map.
    std::map<zx_koid_t, uint32_t> color_buffers_ TA_GUARDED(lock_);
    async::Loop heap_loop_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Control);
};

} // namespace goldfish

#endif // ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_CONTROL_CONTROL_DEVICE_H_
