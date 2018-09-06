// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_INPUT_H_
#define GARNET_LIB_MACHINA_VIRTIO_INPUT_H_

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid/hid.h>
#include <virtio/input.h>
#include <virtio/virtio_ids.h>
#include <zircon/compiler.h>
#include <zircon/device/input.h>
#include <zircon/types.h>

#include "garnet/lib/machina/input_dispatcher.h"
#include "garnet/lib/machina/virtio_device.h"

#define VIRTIO_INPUT_Q_EVENTQ 0
#define VIRTIO_INPUT_Q_STATUSQ 1
#define VIRTIO_INPUT_Q_COUNT 2

namespace machina {

// Virtio input device.
class VirtioInput : public VirtioDevice<VIRTIO_ID_INPUT, VIRTIO_INPUT_Q_COUNT,
                                        virtio_input_config_t> {
 public:
  VirtioQueue* event_queue() { return queue(VIRTIO_INPUT_Q_EVENTQ); }

  // Spawns a thread to monitor for new input devices. When one is detected
  // the corresponding event source will be created to poll for events.
  zx_status_t Start();

 protected:
  VirtioInput(InputEventQueue* event_queue, const PhysMem& phys_mem,
              const char* device_name, const char* device_serial);

  virtual zx_status_t UpdateConfig(uint64_t addr, const IoValue& value);

 protected:
  zx_status_t SendKeyEvent(uint16_t code, virtio_input_key_event_value value);
  zx_status_t SendRepEvent(uint16_t code, virtio_input_key_event_value value);
  zx_status_t SendRelEvent(virtio_input_rel_event_code code, uint32_t value);
  zx_status_t SendAbsEvent(virtio_input_abs_event_code code, uint32_t value);
  zx_status_t SendSynEvent();

 private:
  zx_status_t PollEventQueue();
  zx_status_t SendVirtioEvent(const virtio_input_event_t& event,
                              uint8_t actions);
  // Process a fuchsia event and pass it to the guest.
  // Note: event positions should be normalized such that 0..1 maps to the
  // extents of the guest.
  virtual zx_status_t HandleEvent(const fuchsia::ui::input::InputEvent& event) = 0;

  const char* device_name_;
  const char* device_serial_;
  InputEventQueue* event_queue_;
};

class VirtioKeyboard : public VirtioInput {
 public:
  VirtioKeyboard(InputEventQueue* event_queue, const PhysMem& phys_mem,
                 const char* device_name, const char* device_serial)
      : VirtioInput(event_queue, phys_mem, device_name, device_serial) {}

 private:
  zx_status_t UpdateConfig(uint64_t addr, const IoValue& value) override;
  zx_status_t HandleEvent(const fuchsia::ui::input::InputEvent& event) override;
};

class VirtioRelativePointer : public VirtioInput {
 public:
  VirtioRelativePointer(InputEventQueue* event_queue, const PhysMem& phys_mem,
                        const char* device_name, const char* device_serial)
      : VirtioInput(event_queue, phys_mem, device_name, device_serial) {}

 private:
  zx_status_t UpdateConfig(uint64_t addr, const IoValue& value) override;
  zx_status_t HandleEvent(const fuchsia::ui::input::InputEvent& event) override;
};

class VirtioAbsolutePointer : public VirtioInput {
 public:
  VirtioAbsolutePointer(InputEventQueue* event_queue, const PhysMem& phys_mem,
                        const char* device_name, const char* device_serial)
      : VirtioInput(event_queue, phys_mem, device_name, device_serial) {}

 private:
  zx_status_t UpdateConfig(uint64_t addr, const IoValue& value) override;
  zx_status_t HandleEvent(const fuchsia::ui::input::InputEvent& event) override;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_INPUT_H_
