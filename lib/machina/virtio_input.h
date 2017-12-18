// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_INPUT_H_
#define GARNET_LIB_MACHINA_VIRTIO_INPUT_H_

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <hid/hid.h>
#include <virtio/input.h>
#include <zircon/compiler.h>
#include <zircon/device/input.h>
#include <zircon/types.h>

#include "garnet/lib/machina/input_dispatcher.h"
#include "garnet/lib/machina/virtio.h"

#define VIRTIO_INPUT_Q_EVENTQ 0
#define VIRTIO_INPUT_Q_STATUSQ 1
#define VIRTIO_INPUT_Q_COUNT 2

namespace machina {

// Virtio input device.
class VirtioInput : public VirtioDevice {
 public:
  VirtioInput(InputDispatcher* input_dispatcher,
              uintptr_t guest_physmem_addr,
              size_t guest_physmem_size,
              const char* device_name,
              const char* device_serial);

  zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override;

  virtio_queue_t& event_queue() { return queues_[VIRTIO_INPUT_Q_EVENTQ]; }

  // Spawns a thread to monitor for new input devices. When one is detected
  // the corresponding event source will be created to poll for events.
  zx_status_t Start();

 private:
  zx_status_t PollInputDispatcher();

  zx_status_t OnInputEvent(const InputEvent& event);
  zx_status_t OnBarrierEvent();
  zx_status_t OnKeyEvent(const KeyEvent& event);

  zx_status_t SendVirtioEvent(const virtio_input_event_t& event);

  InputDispatcher* input_dispatcher_;

  fbl::Mutex mutex_;
  const char* device_name_;
  const char* device_serial_;
  virtio_queue_t queues_[VIRTIO_INPUT_Q_COUNT];
  virtio_input_config_t config_ __TA_GUARDED(config_mutex_) = {};
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_INPUT_H_
