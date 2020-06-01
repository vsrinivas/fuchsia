// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
#define SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>

#include <set>

#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace scenic_impl {
namespace input {

struct Extents {
  glm::vec2 min = glm::vec2(0);
  glm::vec2 max = glm::vec2(0);
  Extents() = default;
  Extents(std::array<std::array<float, 2>, 2> extents) {
    min = {extents[0][0], extents[0][1]};
    max = {extents[1][0], extents[1][1]};
  }
};

struct Viewport {
  Extents extents;
  glm::mat3 viewport_to_context_transform = glm::mat3(1.f);
};

// Non-FIDL-type struct for keeping client defined settings.
struct InjectorSettings {
  fuchsia::ui::pointerinjector::DispatchPolicy dispatch_policy =
      fuchsia::ui::pointerinjector::DispatchPolicy(0u);
  uint32_t device_id = 0u;
  fuchsia::ui::pointerinjector::DeviceType device_type =
      fuchsia::ui::pointerinjector::DeviceType(0u);
  zx_koid_t context_koid = ZX_KOID_INVALID;
  zx_koid_t target_koid = ZX_KOID_INVALID;
};

// Implementation of the |fuchsia::ui::pointerinjector::Device| interface. One instance per channel.
class Injector : public fuchsia::ui::pointerinjector::Device {
 public:
  Injector(InjectorSettings settings, Viewport viewport,
           fidl::InterfaceRequest<fuchsia::ui::pointerinjector::Device> injector,
           fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
               is_descendant_and_connected,
           fit::function<void(/*context*/ zx_koid_t, /*target*/ zx_koid_t,
                              /*context_local_event*/ const fuchsia::ui::input::PointerEvent&)>
               inject);

  static bool IsValidConfig(const fuchsia::ui::pointerinjector::Config& config);

  // |fuchsia::ui::pointerinjector::Device|
  void Inject(std::vector<fuchsia::ui::pointerinjector::Event> events,
              InjectCallback callback) override;

  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler);

 private:
  zx_status_t ValidatePointerSample(
      const fuchsia::ui::pointerinjector::PointerSample& pointer_sample);

  // Tracks event streams for each pointer id. Returns true if the event stream is valid, false
  // otherwise.
  // Event streams are expected to start with an ADD, followed by a number of CHANGE events, and
  // ending in either a REMOVE or a CANCEL. Anything else is invalid.
  bool ValidateEventStream(uint32_t pointer_id, fuchsia::ui::pointerinjector::EventPhase phase);

  // Injects a CANCEL event for each ongoing stream and stops tracking them.
  void CancelOngoingStreams();

  // Closes the fidl channel. This triggers the destruction of the Injector object through the
  // error handler set in InputSystem.
  // NOTE: No further method calls or member accesses should be made after CloseChannel(), since
  // they might be made on a destroyed object.
  void CloseChannel(zx_status_t epitaph);

  fidl::Binding<fuchsia::ui::pointerinjector::Device> binding_;

  // Client defined data.
  const InjectorSettings settings_;
  Viewport viewport_;

  // Tracks stream's status (per pointer_id) as it moves through its state machine. Used to
  // validate each event's phase.
  // - ADD: add stream to set
  // - CHANGE: no-op
  // - REMOVE/CANCEL: remove stream from set.
  // Hence, each stream here matches ADD - CHANGE*.
  std::set<uint32_t> ongoing_streams_;

  fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
      is_descendant_and_connected_;

  fit::function<void(/*context*/ zx_koid_t, /*target*/ zx_koid_t,
                     /*context_local_event*/ const fuchsia::ui::input::PointerEvent&)>
      inject_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
