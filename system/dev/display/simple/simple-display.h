// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <ddk/driver.h>
#include <zircon/pixelformat.h>

#if __cplusplus

#include <ddktl/device.h>
#include <ddktl/mmio.h>
#include <ddktl/protocol/display/controller.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/vmo.h>

class SimpleDisplay;
using DeviceType = ddk::Device<SimpleDisplay, ddk::Unbindable>;

class SimpleDisplay : public DeviceType,
                      public ddk::DisplayControllerImplProtocol<SimpleDisplay> {
public:
    SimpleDisplay(zx_device_t* parent, ddk::MmioBuffer framebuffer_mmio,
                  uint32_t width, uint32_t height,
                  uint32_t stride, zx_pixel_format_t format);
    ~SimpleDisplay() = default;

    void DdkUnbind();
    void DdkRelease();
    zx_status_t Bind(const char* name, fbl::unique_ptr<SimpleDisplay>* controller_ptr);

    void DisplayControllerImplSetDisplayControllerInterface(
            const display_controller_interface_t* intf);
    zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);
    void DisplayControllerImplReleaseImage(image_t* image);
    uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                 size_t display_count, uint32_t** layer_cfg_results,
                                                 size_t* layer_cfg_result_count);
    void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                             size_t display_count);
    uint32_t DisplayControllerImplComputeLinearStride(uint32_t width, zx_pixel_format_t format);
    zx_status_t DisplayControllerImplAllocateVmo(uint64_t size, zx::vmo* vmo_out);

private:
    ddk::MmioBuffer framebuffer_mmio_;
    zx_koid_t framebuffer_koid_;

    uint32_t width_;
    uint32_t height_;
    uint32_t stride_;
    zx_pixel_format_t format_;

    ddk::DisplayControllerInterfaceProxy intf_;
};

#endif // __cplusplus

__BEGIN_CDECLS
zx_status_t bind_simple_pci_display(zx_device_t* dev, const char* name, uint32_t bar,
                                    uint32_t width, uint32_t height,
                                    uint32_t stride, zx_pixel_format_t format);

zx_status_t bind_simple_pci_display_bootloader(zx_device_t* dev, const char* name, uint32_t bar);
__END_CDECLS
