// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_INJECTOR_H_
#define SRC_UI_BIN_ROOT_PRESENTER_INJECTOR_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <deque>
#include <unordered_map>

namespace root_presenter {

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

  Injector(sys::ComponentContext* component_context, fuchsia::ui::views::ViewRef context,
           fuchsia::ui::views::ViewRef target);
  ~Injector() = default;

  // Not copyable or movable. Since internal closures capture |this| it's not safe.
  Injector(const Injector&) = delete;
  Injector& operator=(const Injector&) = delete;
  Injector(Injector&&) = delete;
  Injector& operator=(Injector&&) = delete;

  void SetViewport(Viewport viewport);
  void OnDeviceAdded(uint32_t device_id);
  void OnDeviceRemoved(uint32_t device_id);
  void OnEvent(const fuchsia::ui::input::InputEvent& event);

  // To be called when the scene is ready for injection.
  // All events are buffered until this is called to prevent test flakiness.
  void MarkSceneReady();

 private:
  static constexpr uint64_t kLogFrequency = 100u;

  using InjectorId = uint64_t;
  struct PerDeviceInjector {
    uint32_t device_id = std::numeric_limits<uint32_t>::max();
    fuchsia::ui::pointerinjector::DevicePtr touch_injector;
    std::deque<fuchsia::ui::pointerinjector::Event> pending_events;
    bool injection_in_flight = false;
    bool kill_when_empty = false;
  };

  // Called for each new injector device.
  void SetupInputInjection(InjectorId injector_id, uint32_t device_id);

  void InjectPending(InjectorId injector_id);

  fuchsia::ui::pointerinjector::Viewport GetCurrentViewport();

  const sys::ComponentContext* const component_context_;
  const fuchsia::ui::views::ViewRef context_view_ref_;
  const fuchsia::ui::views::ViewRef target_view_ref_;

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
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_INJECTOR_H_
