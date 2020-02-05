// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BUS_VIRTIO_GPU_H_
#define ZIRCON_SYSTEM_DEV_BUS_VIRTIO_GPU_H_

#include <semaphore.h>
#include <stdlib.h>
#include <zircon/compiler.h>
#include <zircon/pixelformat.h>

#include <memory>

#include <ddk/protocol/display/controller.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/display/controller.h>

#include "device.h"
#include "ring.h"
#include "virtio_gpu.h"

namespace virtio {

class Ring;

class GpuDevice;
using DeviceType = ddk::Device<GpuDevice, ddk::GetProtocolable>;
class GpuDevice : public Device,
                  public DeviceType,
                  public ddk::DisplayControllerImplProtocol<GpuDevice, ddk::base_protocol> {
 public:
  GpuDevice(zx_device_t* device, zx::bti bti, std::unique_ptr<Backend> backend);
  virtual ~GpuDevice();

  zx_status_t Init() override;
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkRelease() { virtio::Device::Release(); }

  void IrqRingUpdate() override;
  void IrqConfigChange() override;

  const virtio_gpu_resp_display_info::virtio_gpu_display_one* pmode() const { return &pmode_; }

  void Flush();

  const char* tag() const override { return "virtio-gpu"; }

  zx_status_t GetVmoAndStride(image_t* image, zx_unowned_handle_t handle, uint32_t index,
                              zx::vmo* vmo_out, size_t* offset_out, uint32_t* pixel_size_out,
                              uint32_t* row_bytes_out);

  void DisplayControllerImplSetDisplayControllerInterface(
      const display_controller_interface_protocol_t* intf);

  zx_status_t DisplayControllerImplImportVmoImage(image_t* image, zx::vmo vmo, size_t offset);

  zx_status_t DisplayControllerImplImportImage(image_t* image, zx_unowned_handle_t collection,
                                               uint32_t index);

  void DisplayControllerImplReleaseImage(image_t* image);

  uint32_t DisplayControllerImplCheckConfiguration(const display_config_t** display_config_list,
                                                   size_t display_config_count,
                                                   uint32_t** out_layer_cfg_result_list,
                                                   size_t* layer_cfg_result_count);

  void DisplayControllerImplApplyConfiguration(const display_config_t** display_config_list,
                                               size_t display_config_count);

  zx_status_t DisplayControllerImplGetSysmemConnection(zx::channel sysmem_handle);

  zx_status_t DisplayControllerImplSetBufferCollectionConstraints(const image_t* config,
                                                                  zx_unowned_handle_t collection);

  zx_status_t DisplayControllerImplGetSingleBufferFramebuffer(zx::vmo* out_vmo,
                                                              uint32_t* out_stride);

 private:
  // Internal routines
  template <typename RequestType, typename ResponseType>
  void send_command_response(const RequestType* cmd, ResponseType** res);
  zx_status_t Import(zx::vmo vmo, image_t* image, size_t offset, uint32_t pixel_size,
                     uint32_t row_bytes);

  zx_status_t get_display_info();
  zx_status_t allocate_2d_resource(uint32_t* resource_id, uint32_t width, uint32_t height);
  zx_status_t attach_backing(uint32_t resource_id, zx_paddr_t ptr, size_t buf_len);
  zx_status_t set_scanout(uint32_t scanout_id, uint32_t resource_id, uint32_t width,
                          uint32_t height);
  zx_status_t flush_resource(uint32_t resource_id, uint32_t width, uint32_t height);
  zx_status_t transfer_to_host_2d(uint32_t resource_id, uint32_t width, uint32_t height);

  zx_status_t virtio_gpu_start();

  thrd_t start_thread_ = {};

  // the main virtio ring
  Ring vring_ = {this};

  // gpu op
  io_buffer_t gpu_req_;

  // A saved copy of the display
  virtio_gpu_resp_display_info::virtio_gpu_display_one pmode_ = {};
  int pmode_id_ = -1;

  uint32_t next_resource_id_ = 1;

  fbl::Mutex request_lock_;
  sem_t request_sem_;
  sem_t response_sem_;

  // Flush thread
  void virtio_gpu_flusher();
  thrd_t flush_thread_ = {};
  fbl::Mutex flush_lock_;
  cnd_t flush_cond_ = {};
  bool flush_pending_ = false;

  display_controller_interface_protocol_t dc_intf_;
  sysmem_protocol_t sysmem_;

  struct imported_image* current_fb_;
  struct imported_image* displayed_fb_;

  zx_pixel_format_t supported_formats_ = ZX_PIXEL_FORMAT_RGB_x888;
};

}  // namespace virtio

#endif  // ZIRCON_SYSTEM_DEV_BUS_VIRTIO_GPU_H_
