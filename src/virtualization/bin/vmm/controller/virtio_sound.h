// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_SOUND_H_
#define SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_SOUND_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <virtio/sound.h>
#include <virtio/virtio_ids.h>

#include "src/virtualization/bin/vmm/virtio_device.h"

static constexpr uint16_t kVirtioSoundNumQueues = 4;

class VirtioSound
    : public VirtioComponentDevice<VIRTIO_ID_SOUND, kVirtioSoundNumQueues, virtio_snd_config_t> {
 public:
  explicit VirtioSound(const PhysMem& phys_mem);

  zx_status_t Start(const zx::guest& guest, ::sys::ComponentContext* context,
                    async_dispatcher_t* dispatcher, bool enable_input);

 private:
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::virtualization::hardware::VirtioSoundSyncPtr sound_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                             zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_CONTROLLER_VIRTIO_SOUND_H_
