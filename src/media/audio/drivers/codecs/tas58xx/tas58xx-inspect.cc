// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas58xx-inspect.h"

#include <iomanip>
#include <sstream>

namespace audio {

Tas58xxInspect::Event& Tas58xxInspect::AddEvent() {
  if (events_.size() == kMostRecentCount)
    events_.pop_back();

  return events_.emplace_front();
}

void Tas58xxInspect::ReportEvent(zx::time timestamp, const std::string& state) {
  auto timestamp_integer = static_cast<uint64_t>(timestamp.get());
  int existing_serial_number = 0;
  if (!events_.empty()) {
    Event& event = events_.front();
    if (state == event.state) {
      // State has not changed since last report.
      // Update timestamp and return.
      event.end_time.Set(timestamp_integer);
      return;
    }
    existing_serial_number = event.serial_number;
  }

  Event& event = AddEvent();
  event.serial_number = existing_serial_number + 1;
  event.state = state;
  event.event_node = driver_inspect_.CreateChild(driver_inspect_.UniqueName("event-"));
  event.event_node.CreateInt("first_seen", timestamp_integer, &event.values);
  event.end_time = event.event_node.CreateInt("last_seen", timestamp_integer);
  event.event_node.CreateString("state", state, &event.values);
}

void Tas58xxInspect::ReportFaultFree(zx::time timestamp) { ReportEvent(timestamp, "No fault"); }

void Tas58xxInspect::ReportGpioError(zx::time timestamp) { ReportEvent(timestamp, "GPIO error"); }

void Tas58xxInspect::ReportI2CError(zx::time timestamp) { ReportEvent(timestamp, "I2C error"); }

void Tas58xxInspect::ReportFault(zx::time timestamp, uint8_t chan_fault, uint8_t global_fault1,
                                 uint8_t global_fault2, uint8_t ot_warning) {
  std::ostringstream state;

  // Decode the bits first
  if (chan_fault & 0x01)
    state << "Right channel over current fault, ";
  if (chan_fault & 0x02)
    state << "Left channel over current fault, ";
  if (chan_fault & 0x04)
    state << "Right channel DC fault, ";
  if (chan_fault & 0x08)
    state << "Left channel DC fault, ";
  if (global_fault1 & 0x01)
    state << "PVDD under voltage fault, ";
  if (global_fault1 & 0x02)
    state << "PVDD over voltage fault, ";
  if (global_fault1 & 0x04)
    state << "Clock fault, ";
  if (global_fault1 & 0x40)
    state << "BQ write error, ";
  if (global_fault1 & 0x80)
    state << "OTP CRC check error, ";
  if (global_fault2 & 0x01)
    state << "Over temperature shut down fault, ";
  if (ot_warning & 0x04)
    state << "Over temperature warning, ";

  // At the end, unconditionally output the four register values in hex.
  // This may be useful for diagnosis if the codec is so badly broken
  // that it's returning bogus data.
  state << std::setfill('0') << std::setw(2) << (chan_fault & 0xFF) << " " << std::setw(2)
        << (global_fault1 & 0xFF) << " " << std::setw(2) << (global_fault2 & 0xFF) << " "
        << std::setw(2) << (ot_warning & 0xFF);

  ReportEvent(timestamp, state.str());
}

}  // namespace audio
