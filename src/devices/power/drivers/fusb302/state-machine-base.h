// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_FUSB302_STATE_MACHINE_BASE_H_
#define SRC_DEVICES_POWER_DRIVERS_FUSB302_STATE_MACHINE_BASE_H_

#include <lib/ddk/debug.h>
#include <lib/zx/timer.h>

#include "src/devices/power/drivers/fusb302/usb-pd.h"

namespace fusb302 {

using usb::pd::PdMessage;

// Event: a bitmap of events that have occurred. Filled out by the GetInterrupt() and used by
// StateMachine().
struct Event {
  uint8_t value;

  // Reset event
  DEF_SUBBIT(value, 3, rec_reset);
  // TX event
  DEF_SUBBIT(value, 2, tx);
  // RX event
  DEF_SUBBIT(value, 1, rx);
  // CC event
  DEF_SUBBIT(value, 0, cc);

  explicit Event(uint8_t val) : value(val) {}
};

template <class States, class Device>
class StateMachineBase {
 public:
  explicit StateMachineBase(Device* device) : device_(device) {}
  virtual ~StateMachineBase() = default;

  zx_status_t Run(Event event, std::shared_ptr<PdMessage> message) {
    bool entry;
    // Loop while entry_ is true (meaning we're waiting to enter a state), so that there is no wait
    // delay between states.
    do {
      entry = entry_;
      entry_ = false;
      auto status = RunState(event, message, entry);
      if (status != ZX_OK) {
        zxlogf(ERROR, "RunState failed with %d", status);
        return status;
      }
    } while (entry_);
    return ZX_OK;
  }

  void SetState(States state) {
    zxlogf(DEBUG, "Setting state to %u", state);
    state_ = state;
    entry_ = true;
  }
  States state() { return state_; }
  Device* device() { return device_; }

 private:
  virtual zx_status_t RunState(Event event, std::shared_ptr<PdMessage> message, bool entry) = 0;

  // device_:Pointer to HW device (ex: Fusb302)
  Device* device_;

  // state_: Current state.
  States state_;

  // entry_: Are we waiting to enter into a new state? If so, don't wait for the next timer to fire
  // and run the entry actions.
  bool entry_ = true;
};

}  // namespace fusb302

#endif  // SRC_DEVICES_POWER_DRIVERS_FUSB302_STATE_MACHINE_BASE_H_
