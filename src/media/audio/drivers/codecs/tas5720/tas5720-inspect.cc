// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5720-inspect.h"

#include <iomanip>
#include <sstream>

namespace audio {

Tas5720Inspect::Event& Tas5720Inspect::AddEvent() {
  if (events_.size() == kMostRecentCount)
    events_.pop_back();

  return events_.emplace_front();
}

void Tas5720Inspect::ReportEvent(zx::time timestamp, const std::string& state) {
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

void Tas5720Inspect::ReportFaultFree(zx::time timestamp) { ReportEvent(timestamp, "No fault"); }

void Tas5720Inspect::ReportI2CError(zx::time timestamp) { ReportEvent(timestamp, "I2C error"); }

void Tas5720Inspect::ReportFault(zx::time timestamp, uint8_t fault_bits) {
  std::ostringstream state;

  // Decode the bits first
  if (fault_bits & 0x01)
    state << "Over temperature error, ";
  if (fault_bits & 0x02)
    state << "DC detect error, ";
  if (fault_bits & 0x04)
    state << "Over current error, ";
  if (fault_bits & 0x08)
    state << "SAIF clock error, ";

  // At the end, unconditionally output the fault bits in hex.
  state << std::setfill('0') << std::setw(2) << (fault_bits & 0xFF);

  ReportEvent(timestamp, state.str());
}

}  // namespace audio
