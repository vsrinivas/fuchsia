// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_INSPECT_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_INSPECT_H_

#include <lib/sys/inspect/cpp/component.h>

#include <deque>

namespace audio {

// Container that creates and holds inspect nodes for the Tas58xx driver.
class Tas58xxInspect {
 public:
  Tas58xxInspect(inspect::Inspector& inspector, std::string_view tree_name)
      : driver_inspect_(inspector.GetRoot().CreateChild(tree_name)) {}

  // Called by the driver to report that the codec is fault-free.
  void ReportFaultFree(zx::time timestamp);

  // Called by the driver to report a GPIO error during fault polling.
  void ReportGpioError(zx::time timestamp);

  // Called by the driver to report an I2C error during fault polling.
  void ReportI2CError(zx::time timestamp);

  // Called by the driver to report a fault at the codec.
  void ReportFault(zx::time timestamp, uint8_t chan_fault, uint8_t global_fault1,
                   uint8_t global_fault2, uint8_t ot_warning);

 private:
  // Root for our inspect tree.
  inspect::Node driver_inspect_;

  // Report this many "events" -- older events drop off the back.
  static constexpr int kMostRecentCount = 10;

  // Structure that represents an event.  An event is a period of time
  // with a start time, end time, and a consistent state.
  // For maximum flexibility, we convert the state to a string and
  // we use that here and expose it through inspect.
  struct Event {
    int serial_number;         // Counts upward starting at 1.
    std::string state;         // String description of the event state.
    inspect::Node event_node;  // Inspect node for this event.

    // Some "const" properties are set when we create the event node and never
    // touched again; the end time is a mutable property, which gets updated
    // on the fly as the event persists across multiple polling periods.
    // The const properties are just put into the ValueList container for
    // ease of maintenance, but for the end time, we keep it accessible as
    // a top level field in the Event struct.
    inspect::ValueList values;      // Container for "const" properties.
    inspect::IntProperty end_time;  // Mutable property.
  };

  // The most recent events.
  std::deque<Event> events_;

  // Add a new event, ready for use.  If needed, delete the oldest event,
  // which will also delete the associated inspect information.
  Event& AddEvent();

  // Given a timestamp and a state...  If the state matches the current
  // event, just update the timestamp.  If the state does not match the
  // current state, add a new event and fill it in.
  void ReportEvent(zx::time timestamp, const std::string& state);
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS58XX_TAS58XX_INSPECT_H_
