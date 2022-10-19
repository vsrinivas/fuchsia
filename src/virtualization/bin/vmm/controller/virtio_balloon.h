// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_BALLOON_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_BALLOON_H_

#include <fidl/fuchsia.virtualization.hardware/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <virtio/balloon.h>
#include <virtio/virtio_ids.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

static constexpr uint16_t kVirtioBalloonNumQueues = 4;

class VirtioBalloon
    : public VirtioComponentDevice<VIRTIO_ID_BALLOON, kVirtioBalloonNumQueues,
                                   virtio_balloon_config_t>,
      public fidl::WireAsyncEventHandler<fuchsia_virtualization_hardware::VirtioBalloon>,
      public fuchsia::virtualization::BalloonController {
 public:
  explicit VirtioBalloon(const PhysMem& phys_mem);

  zx_status_t Start(const zx::guest& guest, ::sys::ComponentContext* context,
                    async_dispatcher_t* dispatcher);

  void ConnectToBalloonController(
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> endpoint);

 private:
  fidl::BindingSet<fuchsia::virtualization::BalloonController> bindings_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fidl::WireSharedClient<fuchsia_virtualization_hardware::VirtioBalloon> balloon_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);
  void on_fidl_error(fidl::UnbindInfo error) override;

  // |fuchsia::virtualization::BalloonController|
  void GetBalloonSize(GetBalloonSizeCallback callback) override;
  void RequestNumPages(uint32_t num_pages) override;
  void GetMemStats(GetMemStatsCallback callback) override;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_BALLOON_H_
