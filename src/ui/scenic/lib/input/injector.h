// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
#define SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_

#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/inspect/cpp/inspect.h>

#include <deque>
#include <unordered_map>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"
#include "src/ui/scenic/lib/input/stream_id.h"

namespace scenic_impl::input {

// Non-FIDL-type struct for keeping client defined settings.
struct InjectorSettings {
  fuchsia::ui::pointerinjector::DispatchPolicy dispatch_policy =
      fuchsia::ui::pointerinjector::DispatchPolicy(0u);
  uint32_t device_id = 0u;
  fuchsia::ui::pointerinjector::DeviceType device_type =
      fuchsia::ui::pointerinjector::DeviceType(0u);
  zx_koid_t context_koid = ZX_KOID_INVALID;
  zx_koid_t target_koid = ZX_KOID_INVALID;

  std::optional<fuchsia::input::report::Axis> scroll_v_range;
  std::optional<fuchsia::input::report::Axis> scroll_h_range;
  std::vector<uint8_t> button_identifiers;
};

// Utility that Injectors use to send diagnostics to Inspect.
class InjectorInspector {
 public:
  explicit InjectorInspector(inspect::Node inspect_node);

  void OnPointerInjectorEvent(const fuchsia::ui::pointerinjector::Event& event);

  // How long to track injection history.
  static constexpr uint64_t kNumMinutesOfHistory = 10;

 private:
  struct InspectHistory {
    // The minute this was recorded during. Used as the key for appending new values.
    uint64_t minute_key;
    // Number of injected events during |minute_key|.
    uint64_t num_injected_events;
  };

  void UpdateHistory(zx::time now);
  void ReportStats(inspect::Inspector& inspector) const;

  inspect::Node node_;
  inspect::LazyNode history_stats_node_;

  inspect::ExponentialUintHistogram viewport_event_latency_;
  inspect::ExponentialUintHistogram pointer_event_latency_;

  std::deque<InspectHistory> history_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InjectorInspector);
};

// Implementation of the |fuchsia::ui::pointerinjector::Device| interface. One instance per channel.
class Injector : public fuchsia::ui::pointerinjector::Device {
 public:
  Injector(inspect::Node inspect_node, InjectorSettings settings, Viewport viewport,
           fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> device,
           fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
               is_descendant_and_connected,
           fit::function<void()> on_channel_closed);

  // Check the validity of a Viewport.
  // Returns ZX_OK if valid, otherwise logs an error message and return appropriate error code.
  static zx_status_t IsValidViewport(const fuchsia::ui::pointerinjector::Viewport& viewport);

  // |fuchsia::ui::pointerinjector::Device|
  void Inject(std::vector<fuchsia::ui::pointerinjector::Event> events,
              InjectCallback callback) override;

 protected:
  // Forwards the event to device-specific handler in InputSystem (and eventually the client).
  virtual void ForwardEvent(const fuchsia::ui::pointerinjector::Event& event,
                            StreamId stream_id) = 0;

  // Sends an appropriate Cancel event.
  virtual void CancelStream(uint32_t pointer_id, StreamId stream_id) = 0;

  const InjectorSettings& settings() const { return settings_; }
  const Viewport& viewport() const { return viewport_; }

 private:
  // Return value is either both valid, {ZX_OK, valid stream id} or both
  // invalid: {error, kInvalidStreamId}
  std::pair<zx_status_t, StreamId> ValidatePointerSample(
      const fuchsia::ui::pointerinjector::PointerSample& pointer_sample);

  // Tracks event streams. Returns the id of the event stream if the stream is valid
  // and kInvalidStreamId otherwise.
  // Event streams are expected to start with an ADD, followed by a number of CHANGE events, and
  // ending in either a REMOVE or a CANCEL. Anything else is invalid.
  StreamId ValidateEventStream(uint32_t pointer_id, fuchsia::ui::pointerinjector::EventPhase phase);

  // Injects a CANCEL event for each ongoing stream and stops tracking them.
  void CancelOngoingStreams();

  // Closes the fidl channel. This triggers the destruction of the Injector object through the
  // error handler set in InputSystem.
  // NOTE: No further method calls or member accesses should be made after CloseChannel(), since
  // they might be made on a destroyed object.
  void CloseChannel(zx_status_t epitaph);

  // Client-defined data.
  const InjectorSettings settings_;
  Viewport viewport_;

  fidl::Binding<fuchsia::ui::pointerinjector::Device> binding_;

  // Tracks stream's status (per stream id) as it moves through its state machine. Used to
  // validate each event's phase.
  // - ADD: add stream to set
  // - CHANGE: no-op
  // - REMOVE/CANCEL: remove stream from set.
  // Hence, each stream here matches ADD - CHANGE*.
  std::unordered_map<uint32_t, StreamId> ongoing_streams_;

  fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
      is_descendant_and_connected_;

  // Called both when an error is triggered by either the remote or the local side of the channel.
  // Triggers destruction of this object.
  const fit::function<void()> on_channel_closed_;

  InjectorInspector inspector_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
