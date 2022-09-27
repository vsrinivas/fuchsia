// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_MAGMA_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_MAGMA_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/vmar.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <virtio/virtio_ids.h>

#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"
#include "src/virtualization/bin/vmm/virtio_device.h"

#define VIRTMAGMA_QUEUE_COUNT 1

// Virtio Magma device.
class VirtioMagma
    : public VirtioComponentDevice<VIRTIO_ID_MAGMA, VIRTMAGMA_QUEUE_COUNT, virtio_magma_config_t> {
 public:
  explicit VirtioMagma(const PhysMem& phys_mem);

  zx_status_t Start(const zx::guest& guest, zx::vmar vmar,
                    fidl::InterfaceHandle<fuchsia::virtualization::hardware::VirtioWaylandImporter>
                        wayland_importer,
                    ::sys::ComponentContext* context, async_dispatcher_t* dispatcher);

 private:
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::virtualization::hardware::VirtioMagmaSyncPtr magma_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_MAGMA_H_
