// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmar.h>
#include <zircon/types.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "src/graphics/lib/magma/include/magma_abi/magma.h"
#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"
#include "src/lib/fxl/macros.h"
#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/virtio_magma_generic.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

struct ImageInfoWithToken {
  magma_image_info_t info;
  zx::eventpair token;
};

class VirtioMagma : public VirtioMagmaGeneric,
                    public DeviceBase<VirtioMagma>,
                    public fuchsia::virtualization::hardware::VirtioMagma {
 public:
  explicit VirtioMagma(sys::ComponentContext* context);
  ~VirtioMagma() override = default;

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override;
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                      zx_gpaddr_t used, ConfigureQueueCallback callback) override;
  void NotifyQueue(uint16_t queue) override;

  // |fuchsia::virtualization::hardware::VirtioMagma|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info, zx::vmar vmar,
             fidl::InterfaceHandle<fuchsia::virtualization::hardware::VirtioWaylandImporter>
                 wayland_importer,
             StartCallback callback) override;

 private:
  zx_status_t HandleCommandDescriptors(VirtioDescriptor* request_desc,
                                       VirtioDescriptor* response_desc,
                                       uint32_t* used_out) override;

  zx_status_t Handle_device_import(const virtio_magma_device_import_ctrl_t* request,
                                   virtio_magma_device_import_resp_t* response) override;
  zx_status_t Handle_release_connection(const virtio_magma_release_connection_ctrl_t* request,
                                        virtio_magma_release_connection_resp_t* response) override;
  zx_status_t Handle_release_buffer(const virtio_magma_release_buffer_ctrl_t* request,
                                    virtio_magma_release_buffer_resp_t* response) override;
  zx_status_t Handle_internal_map(const virtio_magma_internal_map_ctrl_t* request,
                                  virtio_magma_internal_map_resp_t* response) override;
  zx_status_t Handle_internal_unmap(const virtio_magma_internal_unmap_ctrl_t* request,
                                    virtio_magma_internal_unmap_resp_t* response) override;
  zx_status_t Handle_internal_map2(const virtio_magma_internal_map2_ctrl_t* request,
                                   virtio_magma_internal_map2_resp_t* response) override;
  zx_status_t Handle_internal_unmap2(const virtio_magma_internal_unmap2_ctrl_t* request,
                                     virtio_magma_internal_unmap2_resp_t* response) override;
  zx_status_t Handle_internal_release_handle(
      const virtio_magma_internal_release_handle_ctrl_t* request,
      virtio_magma_internal_release_handle_resp_t* response) override;
  zx_status_t Handle_poll(const virtio_magma_poll_ctrl_t* request,
                          virtio_magma_poll_resp_t* response) override;
  zx_status_t Handle_export(const virtio_magma_export_ctrl_t* request,
                            virtio_magma_export_resp_t* response) override;
  zx_status_t Handle_import(const virtio_magma_import_ctrl_t* request,
                            virtio_magma_import_resp_t* response) override;
  zx_status_t Handle_execute_command_buffer_with_resources2(
      const virtio_magma_execute_command_buffer_with_resources2_ctrl_t* request,
      virtio_magma_execute_command_buffer_with_resources2_resp_t* response) override;
  zx_status_t Handle_virt_create_image(const virtio_magma_virt_create_image_ctrl_t* request,
                                       virtio_magma_virt_create_image_resp_t* response) override;
  zx_status_t Handle_virt_get_image_info(
      const virtio_magma_virt_get_image_info_ctrl_t* request,
      virtio_magma_virt_get_image_info_resp_t* response) override;

  zx::vmar vmar_;
  VirtioQueue out_queue_;
  fuchsia::virtualization::hardware::VirtioWaylandImporterSyncPtr wayland_importer_;
  std::unordered_multimap<zx_koid_t, std::pair<zx_vaddr_t, size_t>> buffer_maps_;
  std::unordered_map<zx_vaddr_t, std::pair<zx_handle_t, size_t>> buffer_maps2_;

  // We store handles that are returned to the client to keep them alive.
  std::list<zx::handle> stored_handles_;

  // Each connection maps images to info, populated either when image is created or imported
  using ImageMap = std::unordered_map<magma_buffer_t, ImageInfoWithToken>;
  std::unordered_map<magma_connection_t, ImageMap> connection_image_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VirtioMagma);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_
