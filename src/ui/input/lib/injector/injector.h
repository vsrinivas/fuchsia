// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_LIB_INJECTOR_INJECTOR_H_
#define SRC_UI_INPUT_LIB_INJECTOR_INJECTOR_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/cpp/component_context.h>

#include <deque>
#include <unordered_map>

#include "src/lib/fxl/macros.h"

namespace input {

// Utility that Injectors use to send diagnostics to Inspect.
class InjectorInspector {
 public:
  explicit InjectorInspector(inspect::Node inspect_node);

  void OnInjectedEvents(uint64_t num_events);
  void OnInjectPendingCancelled(bool injection_in_flight, bool pending_events_empty,
                                bool scene_not_ready);

  // How long to track injection history.
  static constexpr uint64_t kNumMinutesOfHistory = 10;

 private:
  struct InspectHistory {
    // The minute this was recorded during. Used as the key for appending new values.
    uint64_t minute_key = 0;
    // Number of injected events during |minute_key|.
    uint64_t num_injected_events = 0;
  };

  void ReportStats(inspect::Inspector& inspector) const;

  inspect::Node node_;
  inspect::LazyNode history_stats_node_;
  inspect::Node cancelled_injections_node_;

  inspect::UintProperty total_cancelled_injections_;
  inspect::UintProperty injection_in_flight_count_;
  inspect::UintProperty pending_events_empty_count_;
  inspect::UintProperty scene_not_ready_count_;

  std::deque<InspectHistory> history_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InjectorInspector);
};

// Class for handling input injection into Scenic.
//
// We register a touch injector with Scenic for each |device_id| added with OnDeviceAdded.
// On OnDeviceRemoved we close the touch injector channel only after all pending events have been
// delivered.
//
// If a touch injector channel is closed by Scenic, we try to recover by re-establishing the
// connection and injecting pending events. To reduce the risk to performance and infinite loops
// here, we assume the connection is successful, and in the rare case that it's not we drop the
// corresponding events and try to again for the next batch.
//
// We reuse the same context, target, viewport and dispatch policy for all
// fuchsia::ui::pointerinjector::Device channels, since we assume all devices to represent the same
// touchscreen.
class Injector {
 public:
  // Struct used to define the Viewport used for injection. We define an axis-aligned viewport with
  // dimensions of (0,0) to (width, height), scaled and offset from the context view.
  struct Viewport {
    float width = 1.f;
    float height = 1.f;
    float scale = 1.f;
    float x_offset = 0.f;
    float y_offset = 0.f;
  };

  // |component_context| must be not nullptr and must outlive this object. |context|, |target| and
  // |policy| are used to configure the injector. Please see |fuchsia.ui.pointerinjector| for full
  // documentation.
  Injector(sys::ComponentContext* component_context, fuchsia::ui::views::ViewRef context,
           fuchsia::ui::views::ViewRef target,
           fuchsia::ui::pointerinjector::DispatchPolicy policy =
               fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET,
           inspect::Node inspect_node = inspect::Node());
  virtual ~Injector() = default;

  // Not copyable or movable. Since internal closures capture |this| it's not safe.
  Injector(const Injector&) = delete;
  Injector& operator=(const Injector&) = delete;
  Injector(Injector&&) = delete;
  Injector& operator=(Injector&&) = delete;

  virtual void SetViewport(Viewport viewport);
  fuchsia::ui::pointerinjector::Viewport GetCurrentViewport() const;
  virtual void OnDeviceAdded(uint32_t device_id);
  virtual void OnDeviceRemoved(uint32_t device_id);
  virtual void OnEvent(const fuchsia::ui::input::InputEvent& event);

  // To be called when the scene is ready for injection.
  // All events are buffered until this is called to prevent test flakiness.
  virtual void MarkSceneReady();

  // For tests.
  bool scene_ready() const { return scene_ready_; }

 protected:
  // For mocks.
  Injector();

 private:
  static constexpr uint64_t kLogFrequency = 100u;

  using InjectorId = uint64_t;
  struct PerDeviceInjector {
    uint32_t device_id = std::numeric_limits<uint32_t>::max();
    fuchsia::ui::pointerinjector::DevicePtr touch_injector;
    std::deque<fuchsia::ui::pointerinjector::Event> pending_events;
    bool injecting_first_event = true;
    bool injection_in_flight = false;
    bool kill_when_empty = false;
  };

  // Called for each new injector device.
  void SetupInputInjection(InjectorId injector_id, uint32_t device_id);

  void InjectPending(InjectorId injector_id);

  const sys::ComponentContext* const component_context_;
  const fuchsia::ui::views::ViewRef context_view_ref_;
  const fuchsia::ui::views::ViewRef target_view_ref_;
  const fuchsia::ui::pointerinjector::DispatchPolicy policy_ =
      fuchsia::ui::pointerinjector::DispatchPolicy::TOP_HIT_AND_ANCESTORS_IN_TARGET;

  // Flaps once, from false to true.
  // If scene is disturbed, then Presentation and Injector are both destroyed and recreated.
  bool scene_ready_ = false;

  Viewport viewport_;

  // These internal ID's are never reused, even when the same device is added multiple times.
  InjectorId next_injector_id_ = 0;

  // Map of all currently active devices to their corresponding |injector_id|.
  std::unordered_map<uint32_t, InjectorId> injector_id_by_device_id_;
  // Map of all injectors, either with active devices or inactive but with pending events.
  std::unordered_map<InjectorId, PerDeviceInjector> injectors_;

  // Failed injection attempt counter. Used to reduce log spam.
  // We show one log for every |kLogFrequency| failed attempts, and one for every successful
  // recovery.
  uint64_t num_failed_injection_attempts_ = 0u;

  InjectorInspector injector_inspector_;
};

}  // namespace input

#endif  // SRC_UI_INPUT_LIB_INJECTOR_INJECTOR_H_
