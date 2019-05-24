// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_GOLDFISH_DISPLAY_DISPLAY_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_GOLDFISH_DISPLAY_DISPLAY_H_

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddktl/device.h>
#include <ddktl/protocol/display/controller.h>
#include <ddktl/protocol/goldfish/control.h>
#include <ddktl/protocol/goldfish/pipe.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <lib/zx/pmt.h>
#include <threads.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

namespace goldfish {

class Display;
using DisplayType = ddk::Device<Display, ddk::Unbindable>;

class Display
    : public DisplayType,
      public ddk::DisplayControllerImplProtocol<Display, ddk::base_protocol> {
public:
    static zx_status_t Create(void* ctx, zx_device_t* parent);

    explicit Display(zx_device_t* parent);
    ~Display();

    zx_status_t Bind();

    // Device protocol implementation.
    void DdkUnbind();
    void DdkRelease();

    // Display controller protocol implementation.
    void DisplayControllerImplSetDisplayControllerInterface(
        const display_controller_interface_protocol_t* interface);
    zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo,
                                                    size_t offset);
    zx_status_t DisplayControllerImplImportImage(image_t* image,
                                                 zx_unowned_handle_t handle,
                                                 uint32_t index);
    void DisplayControllerImplReleaseImage(image_t* image);
    uint32_t DisplayControllerImplCheckConfiguration(
        const display_config_t** display_configs, size_t display_count,
        uint32_t** layer_cfg_results, size_t* layer_cfg_result_count);
    void DisplayControllerImplApplyConfiguration(
        const display_config_t** display_config, size_t display_count);
    uint32_t DisplayControllerImplComputeLinearStride(uint32_t width,
                                                      zx_pixel_format_t format);
    zx_status_t DisplayControllerImplAllocateVmo(uint64_t size,
                                                 zx::vmo* vmo_out);
    zx_status_t
    DisplayControllerImplGetSysmemConnection(zx::channel connection);
    zx_status_t
    DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                        uint32_t collection);
    zx_status_t
    DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                    uint32_t* out_stride);

private:
    struct ColorBuffer {
        uint32_t id = 0;
        zx_paddr_t paddr = 0;
        size_t size = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        zx::vmo vmo;
        zx::pmt pmt;
    };

    static void OnSignal(void* ctx, int32_t flags);
    void OnReadable();

    void WriteLocked(uint32_t cmd_size) TA_REQ(lock_);
    zx_status_t ReadResultLocked(uint32_t* result) TA_REQ(lock_);
    zx_status_t ExecuteCommandLocked(uint32_t cmd_size, uint32_t* result)
        TA_REQ(lock_);
    int32_t GetFbParamLocked(uint32_t param, int32_t default_value)
        TA_REQ(lock_);
    zx_status_t CreateColorBufferLocked(uint32_t width, uint32_t height,
                                        uint32_t* id) TA_REQ(lock_);
    void OpenColorBufferLocked(uint32_t id) TA_REQ(lock_);
    void CloseColorBufferLocked(uint32_t id) TA_REQ(lock_);
    zx_status_t UpdateColorBufferLocked(uint32_t id, zx_paddr_t paddr,
                                        uint32_t width, uint32_t height,
                                        size_t size, uint32_t* result)
        TA_REQ(lock_);
    void FbPostLocked(uint32_t id) TA_REQ(lock_);

    int FlushHandler();

    fbl::Mutex lock_;
    fbl::ConditionVariable readable_cvar_;
    ddk::GoldfishControlProtocolClient control_ TA_GUARDED(lock_);
    ddk::GoldfishPipeProtocolClient pipe_ TA_GUARDED(lock_);
    int32_t id_ = 0;
    zx::bti bti_;
    ddk::IoBuffer cmd_buffer_ TA_GUARDED(lock_);
    ddk::IoBuffer io_buffer_ TA_GUARDED(lock_);

    thrd_t flush_thread_{};
    fbl::Mutex flush_lock_;
    ColorBuffer* current_fb_ TA_GUARDED(flush_lock_) = nullptr;
    ddk::DisplayControllerInterfaceProtocolClient
        dc_intf_ TA_GUARDED(flush_lock_);
    uint32_t width_ TA_GUARDED(flush_lock_) = 1024;
    uint32_t height_ TA_GUARDED(flush_lock_) = 768;
    bool shutdown_ TA_GUARDED(flush_lock_) = false;

    DISALLOW_COPY_ASSIGN_AND_MOVE(Display);
};

} // namespace goldfish

#endif // ZIRCON_SYSTEM_DEV_DISPLAY_GOLDFISH_DISPLAY_DISPLAY_H_
