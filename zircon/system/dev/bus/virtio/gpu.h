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

#include "device.h"
#include "ring.h"
#include "virtio_gpu.h"

namespace virtio {

class Ring;

class GpuDevice : public Device {
 public:
  GpuDevice(zx_device_t* device, zx::bti bti, std::unique_ptr<Backend> backend);
  virtual ~GpuDevice();

  zx_status_t Init() override;

  void IrqRingUpdate() override;
  void IrqConfigChange() override;

  const virtio_gpu_resp_display_info::virtio_gpu_display_one* pmode() const { return &pmode_; }
  display_controller_impl_protocol_ops_t* get_proto_ops() { return &proto_ops; }

  void Flush();

  const char* tag() const override { return "virtio-gpu"; }

  zx_status_t GetVmoAndStride(image_t* image, zx_unowned_handle_t handle, uint32_t index,
                              zx::vmo* vmo_out, size_t* offset_out, uint32_t* pixel_size_out,
                              uint32_t* row_bytes_out);

 private:
  // DDK driver hooks
  static void virtio_gpu_set_display_controller_interface(
      void* ctx, const display_controller_interface_protocol_t* intf);
  static zx_status_t virtio_gpu_import_vmo_image(void* ctx, image_t* image, zx_handle_t vmo,
                                                 size_t offset);
  static zx_status_t virtio_gpu_import_image(void* ctx, image_t* image, zx_unowned_handle_t handle,
                                             uint32_t index);
  static void virtio_gpu_release_image(void* ctx, image_t* image);
  static uint32_t virtio_gpu_check_configuration(void* ctx,
                                                 const display_config_t** display_configs,
                                                 size_t display_count, uint32_t** layer_cfg_results,
                                                 size_t* layer_cfg_result_count);
  static void virtio_gpu_apply_configuration(void* ctx, const display_config_t** display_configs,
                                             size_t display_count);
  static zx_status_t virtio_get_sysmem_connection(void* ctx, zx_handle_t handle);
  static zx_status_t virtio_set_buffer_collection_constraints(void* ctx, const image_t* config,
                                                              zx_unowned_handle_t collection);
  static zx_status_t virtio_get_single_buffer_framebuffer(void* ctx, zx_handle_t* out_vmo,
                                                          uint32_t* out_stride);

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

  static display_controller_impl_protocol_ops_t proto_ops;

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
