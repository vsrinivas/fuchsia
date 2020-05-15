// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
#define SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerflow/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>

#include <set>

namespace scenic_impl {
namespace input {

using InjectorId = uint64_t;

// Non-FIDL-type struct for keeping client defined settings.
struct InjectorSettings {
  fuchsia::ui::pointerflow::DispatchPolicy dispatch_policy;
  uint32_t device_id;
  fuchsia::ui::pointerflow::DeviceType device_type;
  zx_koid_t context_koid;
  zx_koid_t target_koid;
};

// Implementation of the |fuchsia::ui::pointerflow::Injector| interface. One instance per channel.
class Injector : public fuchsia::ui::pointerflow::Injector {
 public:
  Injector(InjectorId id, InjectorSettings settings,
           fidl::InterfaceRequest<fuchsia::ui::pointerflow::Injector> injector,
           fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
               is_descendant_and_connected,
           fit::function<void(/*context*/ zx_koid_t, /*target*/ zx_koid_t,
                              /*context_local_event*/ const fuchsia::ui::input::PointerEvent&)>
               inject);

  // |fuchsia::ui::pointerflow::Injector|
  void Inject(std::vector<fuchsia::ui::pointerflow::Event> events,
              InjectCallback callback) override;

  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler);

 private:
  // Tracks event streams for each pointer id. Returns true if the event stream is valid, false
  // otherwise.
  // Event streams are expected to start with an ADD, followed by a number of CHANGE events, and
  // ending in either a REMOVE or a CANCEL. Anything else is invalid.
  bool ValidateEventStream(uint32_t pointer_id, fuchsia::ui::pointerflow::EventPhase phase);

  // Injects a CANCEL event for each ongoing stream and stops tracking them.
  void CancelOngoingStreams();

  // Closes the fidl channel. This triggers the destruction of the Injector object through the
  // error handler set in InputSystem.
  // NOTE: No further method calls or member accesses should be made after CloseChannel(), since
  // they might be made on a destroyed object.
  void CloseChannel(zx_status_t epitaph);

  fidl::Binding<fuchsia::ui::pointerflow::Injector> binding_;

  // Scenic-internal identifier.
  const InjectorId id_;

  // Client defined data.
  const InjectorSettings settings_;

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
