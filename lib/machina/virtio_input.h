// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_INPUT_H_
#define GARNET_LIB_MACHINA_VIRTIO_INPUT_H_

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
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
class VirtioInput
    : public VirtioDeviceBase<VIRTIO_ID_INPUT, VIRTIO_INPUT_Q_COUNT,
                              virtio_input_config_t> {
 public:
  VirtioInput(InputEventQueue* event_queue, const PhysMem& phys_mem,
              const char* device_name, const char* device_serial);

  zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override;

  VirtioQueue* event_queue() { return queue(VIRTIO_INPUT_Q_EVENTQ); }

  // Spawns a thread to monitor for new input devices. When one is detected
  // the corresponding event source will be created to poll for events.
  zx_status_t Start();

 private:
  zx_status_t PollEventQueue();

  zx_status_t OnInputEvent(const InputEvent& event);
  zx_status_t OnPointerEvent(const PointerEvent& pointer_event);
  zx_status_t OnBarrierEvent();
  zx_status_t OnKeyEvent(const KeyEvent& event);
  zx_status_t OnButtonEvent(const ButtonEvent& button_event);

  zx_status_t SendVirtioEvent(const virtio_input_event_t& event);

  const char* device_name_;
  const char* device_serial_;
  InputEventQueue* event_queue_;
};

class VirtioKeyboard : public VirtioInput {
 public:
  VirtioKeyboard(InputEventQueue* event_queue, const PhysMem& phys_mem,
                 const char* device_name, const char* device_serial)
      : VirtioInput(event_queue, phys_mem, device_name, device_serial) {}

  zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override;
};

class VirtioRelativePointer : public VirtioInput {
 public:
  VirtioRelativePointer(InputEventQueue* event_queue, const PhysMem& phys_mem,
                        const char* device_name, const char* device_serial)
      : VirtioInput(event_queue, phys_mem, device_name, device_serial) {}

  zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override;
};

class VirtioAbsolutePointer : public VirtioInput {
 public:
  VirtioAbsolutePointer(InputEventQueue* event_queue, const PhysMem& phys_mem,
                        const char* device_name, const char* device_serial,
                        uint32_t max_width, uint32_t max_height)
      : VirtioInput(event_queue, phys_mem, device_name, device_serial),
        max_width_(max_width),
        max_height_(max_height) {}

  zx_status_t WriteConfig(uint64_t addr, const IoValue& value) override;

 private:
  uint32_t max_width_;
  uint32_t max_height_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_INPUT_H_
