// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
#define SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_

#include <fuchsia/ui/pointerinjector/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>

#include <unordered_map>

#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"

namespace scenic_impl {
namespace input {

using StreamId = uint64_t;
constexpr StreamId kInvalidStreamId = 0;
StreamId NewStreamId();

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
           fit::function<void(const InternalPointerEvent&, StreamId stream_id)> inject);

  static bool IsValidConfig(const fuchsia::ui::pointerinjector::Config& config);

  // |fuchsia::ui::pointerinjector::Device|
  void Inject(std::vector<fuchsia::ui::pointerinjector::Event> events,
              InjectCallback callback) override;

  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler);

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

  fidl::Binding<fuchsia::ui::pointerinjector::Device> binding_;

  // Client defined data.
  const InjectorSettings settings_;
  Viewport viewport_;

  // Tracks stream's status (per stream id) as it moves through its state machine. Used to
  // validate each event's phase.
  // - ADD: add stream to set
  // - CHANGE: no-op
  // - REMOVE/CANCEL: remove stream from set.
  // Hence, each stream here matches ADD - CHANGE*.
  std::unordered_map<uint32_t, StreamId> ongoing_streams_;

  fit::function<bool(/*descendant*/ zx_koid_t, /*ancestor*/ zx_koid_t)>
      is_descendant_and_connected_;

  fit::function<void(const InternalPointerEvent&, StreamId)> inject_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INJECTOR_H_
