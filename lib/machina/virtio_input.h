// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_INPUT_H_
#define GARNET_LIB_MACHINA_VIRTIO_INPUT_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid/hid.h>
#include <virtio/input.h>
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/device/input.h>
#include <zircon/types.h>

#include "garnet/lib/machina/input_dispatcher_impl.h"
#include "garnet/lib/machina/virtio_device.h"

#define VIRTIO_INPUT_Q_EVENTQ 0
#define VIRTIO_INPUT_Q_STATUSQ 1
#define VIRTIO_INPUT_Q_COUNT 2

namespace machina {

// Virtio input device.
class VirtioInput
    : public VirtioInprocessDevice<VIRTIO_ID_INPUT, VIRTIO_INPUT_Q_COUNT,
                                   virtio_input_config_t> {
 public:
  VirtioInput(InputEventQueue* event_queue, const PhysMem& phys_mem);

  zx_status_t Start();

 private:
  VirtioQueue* event_queue() { return queue(VIRTIO_INPUT_Q_EVENTQ); }

  zx_status_t UpdateConfig(uint64_t addr, const IoValue& value);

  zx_status_t SendKeyEvent(uint16_t code, virtio_input_key_event_value value);
  zx_status_t SendRepEvent(uint16_t code, virtio_input_key_event_value value);
  zx_status_t SendAbsEvent(virtio_input_abs_event_code code, uint32_t value);
  zx_status_t SendSynEvent();

  zx_status_t SendVirtioEvent(const virtio_input_event_t& event,
                              uint8_t actions);
  zx_status_t PollEventQueue();

  zx_status_t HandleKeyEvent(const fuchsia::ui::input::InputEvent& event);
  zx_status_t HandleAbsEvent(const fuchsia::ui::input::InputEvent& event);
  // Process a fuchsia event and pass it to the guest.
  zx_status_t HandleEvent(const fuchsia::ui::input::InputEvent& event);

  InputEventQueue* event_queue_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_INPUT_H_
