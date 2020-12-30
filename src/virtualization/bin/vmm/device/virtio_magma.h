// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/vmar.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "src/graphics/lib/magma/include/magma_abi/magma.h"
#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"
#include "src/lib/fxl/macros.h"
#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/virtio_magma_generic.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

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
  zx_status_t Handle_device_import(const virtio_magma_device_import_ctrl_t* request,
                                   virtio_magma_device_import_resp_t* response) override;
  zx_status_t Handle_internal_map(const virtio_magma_internal_map_ctrl_t* request,
                                  virtio_magma_internal_map_resp_t* response) override;
  zx_status_t Handle_internal_unmap(const virtio_magma_internal_unmap_ctrl_t* request,
                                    virtio_magma_internal_unmap_resp_t* response) override;
  zx_status_t Handle_poll(const virtio_magma_poll_ctrl_t* request,
                          virtio_magma_poll_resp_t* response) override;
  zx_status_t Handle_read_notification_channel(
      const virtio_magma_read_notification_channel_ctrl_t* request,
      virtio_magma_read_notification_channel_resp_t* response) override;
  zx_status_t Handle_export(const virtio_magma_export_ctrl_t* request,
                            virtio_magma_export_resp_t* response) override;
  zx_status_t Handle_execute_command_buffer_with_resources(
      const virtio_magma_execute_command_buffer_with_resources_ctrl_t* request,
      virtio_magma_execute_command_buffer_with_resources_resp_t* response) override;

  zx::vmar vmar_;
  VirtioQueue out_queue_;
  fuchsia::virtualization::hardware::VirtioWaylandImporterSyncPtr wayland_importer_;
  std::unordered_multimap<zx_koid_t, std::pair<zx_vaddr_t, size_t>> buffer_maps_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VirtioMagma);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_
