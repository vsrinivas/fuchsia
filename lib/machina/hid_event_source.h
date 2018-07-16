// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_HID_EVENT_SOURCE_H_
#define GARNET_LIB_MACHINA_HID_EVENT_SOURCE_H_

#include <utility>

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <hid/hid.h>

#include "garnet/lib/machina/input_dispatcher.h"

namespace machina {

// Manages input events from a single (host) HID device.
class HidInputDevice
    : public fbl::SinglyLinkedListable<fbl::unique_ptr<HidInputDevice>> {
 public:
  HidInputDevice(InputDispatcher* input_dispatcher, fbl::unique_fd fd)
      : fd_(std::move(fd)), input_dispatcher_(input_dispatcher) {}

  explicit HidInputDevice(InputDispatcher* input_dispatcher)
      : input_dispatcher_(input_dispatcher) {}

  // Spawn a thread to read key reports from the keyboard device.
  zx_status_t Start();

  // Compares |keys| against the previous report to infer which keys have
  // been pressed or released. Sends a corresponding evdev event for each
  // key press/release.
  zx_status_t HandleHidKeys(const hid_keys_t& keys);

 private:
  zx_status_t HidEventLoop();
  void SendKeyEvent(uint32_t scancode, bool pressed);
  void SendBarrier(InputEventQueue* event_queue);

  fbl::unique_fd fd_;
  hid_keys_t prev_keys_ = {};
  InputDispatcher* input_dispatcher_;
};

class HidEventSource {
 public:
  HidEventSource(InputDispatcher* input_dispatcher)
      : input_dispatcher_(input_dispatcher) {}

  zx_status_t Start();

 private:
  static zx_status_t WatchInputDirectory(void* arg);

  // Invoked whenever a new node appears under the /dev/class/input directory.
  zx_status_t AddInputDevice(int dirfd, int event, const char* fn);

  InputDispatcher* input_dispatcher_;
  fbl::Mutex mutex_;
  fbl::SinglyLinkedList<fbl::unique_ptr<HidInputDevice>> devices_
      __TA_GUARDED(mutex_);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_HID_EVENT_SOURCE_H_
