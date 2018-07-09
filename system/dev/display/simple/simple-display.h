// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <ddk/driver.h>
#include <zircon/pixelformat.h>

#if __cplusplus

#include <ddktl/device.h>
#include <ddktl/protocol/display-controller.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/vmo.h>

class SimpleDisplay;
using DeviceType = ddk::Device<SimpleDisplay, ddk::Unbindable>;

class SimpleDisplay : public DeviceType,
                      public ddk::DisplayControllerProtocol<SimpleDisplay> {
public:
    SimpleDisplay(zx_device_t* parent, zx_handle_t vmo,
                  uint32_t width, uint32_t height,
                  uint32_t stride, zx_pixel_format_t format);

    void DdkUnbind();
    void DdkRelease();
    zx_status_t Bind(const char* name, fbl::unique_ptr<SimpleDisplay>* controller_ptr);

    void SetDisplayControllerCb(void* cb_ctx, display_controller_cb_t* cb);
    zx_status_t GetDisplayInfo(uint64_t display_id, display_info_t* info);
    zx_status_t ImportVmoImage(image_t* image, const zx::vmo& vmo, size_t offset);
    void ReleaseImage(image_t* image);
    void CheckConfiguration(const display_config_t** display_config,
                            uint32_t* display_cfg_result, uint32_t** layer_cfg_result,
                            uint32_t display_count);
    void ApplyConfiguration(const display_config_t** display_config, uint32_t display_count);
    uint32_t ComputeLinearStride(uint32_t width, zx_pixel_format_t format);
    zx_status_t AllocateVmo(uint64_t size, zx_handle_t* vmo_out);

private:
    zx::vmo framebuffer_handle_;
    zx_koid_t framebuffer_koid_;

    uint32_t width_;
    uint32_t height_;
    uint32_t stride_;
    zx_pixel_format_t format_;

    display_controller_cb_t* cb_;
    void* cb_ctx_;
};

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t bind_simple_pci_display(zx_device_t* dev, const char* name, uint32_t bar,
                                    uint32_t width, uint32_t height,
                                    uint32_t stride, zx_pixel_format_t format);

zx_status_t bind_simple_pci_display_bootloader(zx_device_t* dev, const char* name, uint32_t bar);
__END_CDECLS
