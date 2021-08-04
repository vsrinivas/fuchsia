// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_SIMPLE_SIMPLE_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_SIMPLE_SIMPLE_DISPLAY_H_

#include <lib/ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/pixelformat.h>

#if __cplusplus

#include <fuchsia/hardware/display/controller/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/function.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>

#include <atomic>
#include <memory>

#include <ddktl/device.h>

class SimpleDisplay;
using DeviceType = ddk::Device<SimpleDisplay>;
using HeapServer = fidl::WireServer<fuchsia_sysmem2::Heap>;

class SimpleDisplay : public DeviceType,
                      public HeapServer,
                      public ddk::DisplayControllerImplProtocol<SimpleDisplay, ddk::base_protocol> {
 public:
  SimpleDisplay(zx_device_t* parent, sysmem_protocol_t sysmem, ddk::MmioBuffer framebuffer_mmio,
                uint32_t width, uint32_t height, uint32_t stride, zx_pixel_format_t format);
  ~SimpleDisplay() = default;

  void DdkRelease();
  zx_status_t Bind(const char* name, std::unique_ptr<SimpleDisplay>* controller_ptr);

  void AllocateVmo(AllocateVmoRequestView request, AllocateVmoCompleter::Sync& completer) override;
  void CreateResource(CreateResourceRequestView request,
                      CreateResourceCompleter::Sync& completer) override;
  void DestroyResource(DestroyResourceRequestView request,
                       DestroyResourceCompleter::Sync& completer) override;

  void DisplayControllerImplSetDisplayControllerInterface(
      const display_controller_interface_protocol_t* intf);
  zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);
  zx_status_t DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t handle,
                                               uint32_t index);
  void DisplayControllerImplReleaseImage(image_t* image);
  uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_configs,
                                                   size_t display_count,
                                                   uint32_t** layer_cfg_results,
                                                   size_t* layer_cfg_result_count);
  void DisplayControllerImplApplyConfiguration(const display_config_t** display_config,
                                               size_t display_count);
  void DisplayControllerImplSetEld(uint64_t display_id, const uint8_t* raw_eld_list,
                                   size_t raw_eld_count) {}  // No ELD required for non-HDA systems.
  uint32_t DisplayControllerImplComputeLinearStride(uint32_t width, zx_pixel_format_t format);
  zx_status_t DisplayControllerImplAllocateVmo(uint64_t size, zx::vmo* vmo_out);
  zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel connection);
  zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                  uint32_t collection);
  zx_status_t DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                              uint32_t* out_stride);

 private:
  void OnPeriodicVSync();

  sysmem_protocol_t sysmem_;
  async::Loop loop_;

  static_assert(std::atomic<zx_koid_t>::is_always_lock_free);
  std::atomic<zx_koid_t> framebuffer_koid_;
  static_assert(std::atomic<bool>::is_always_lock_free);
  std::atomic<bool> has_image_;

  const ddk::MmioBuffer framebuffer_mmio_;
  const uint32_t width_;
  const uint32_t height_;
  const uint32_t stride_;
  const zx_pixel_format_t format_;

  // Only used on the vsync thread.
  zx::time next_vsync_time_;
  ddk::DisplayControllerInterfaceProtocolClient intf_;
};

#endif  // __cplusplus

__BEGIN_CDECLS
zx_status_t bind_simple_pci_display(zx_device_t* dev, const char* name, uint32_t bar,
                                    uint32_t width, uint32_t height, uint32_t stride,
                                    zx_pixel_format_t format);

zx_status_t bind_simple_pci_display_bootloader(zx_device_t* dev, const char* name, uint32_t bar);
__END_CDECLS

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_SIMPLE_SIMPLE_DISPLAY_H_
