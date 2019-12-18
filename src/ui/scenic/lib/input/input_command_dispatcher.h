// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_
#define SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/accessibility/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <functional>
#include <memory>
#include <unordered_map>

#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/input/pointer_event_buffer.h"

namespace scenic_impl {
namespace input {

class InputSystem;

// Per-session treatment of input commands.
// Routes input events from a root presenter to Scenic clients.
// Manages input-related state, such as focus.
//
// The general flow of events is:
// If accessibility is off:
// DispatchCommand --[decide what/where]--> EnqueueEvent
// If accessibility is on:
// DispatchCommand --> accessibility --[does accessibility want to block it? then stop]--[otherwise
// decide where else to send]--> EnqueueEvent
class InputCommandDispatcher : public CommandDispatcher {
 public:
  InputCommandDispatcher(CommandDispatcherContext context, gfx::Engine* engine,
                         InputSystem* input_system);
  ~InputCommandDispatcher() override = default;

  // |CommandDispatcher|
  void SetDebugName(const std::string& debug_name) override {}

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command) override;

 private:
  // Per-command dispatch logic.
  void DispatchCommand(const fuchsia::ui::input::SendPointerInputCmd& command);
  void DispatchCommand(const fuchsia::ui::input::SendKeyboardInputCmd& command);
  void DispatchCommand(const fuchsia::ui::input::SetHardKeyboardDeliveryCmd& command);
  void DispatchCommand(const fuchsia::ui::input::SetParallelDispatchCmd& command);

  // Per-pointer-type dispatch logic.
  void DispatchTouchCommand(const fuchsia::ui::input::SendPointerInputCmd& command);
  void DispatchMouseCommand(const fuchsia::ui::input::SendPointerInputCmd& command);

  // Dispatches an event to a parallel set of views; set may be empty.
  // Conditionally trigger focus change request, based on |views_and_event.event.phase|.
  // Called by PointerEventBuffer.
  void DispatchDeferredPointerEvent(PointerEventBuffer::DeferredPointerEvent views_and_event);

  // Enqueue the pointer event into the entry in a ViewStack.
  static void ReportPointerEvent(const ViewStack::Entry& view_info,
                                 const fuchsia::ui::input::PointerEvent& pointer);

  // Enqueue the keyboard event into an EventReporter.
  static void ReportKeyboardEvent(EventReporter* reporter,
                                  fuchsia::ui::input::KeyboardEvent keyboard);

  // Enqueue the keyboard event to the IME Service.
  static void ReportToImeService(const fuchsia::ui::input::ImeServicePtr& ime_service,
                                 fuchsia::ui::input::KeyboardEvent keyboard);

  // Retrieve focused ViewRef's KOID from the scene graph.
  // Return ZX_KOID_INVALID if scene does not exist, or if the focus chain is empty.
  zx_koid_t focus() const;

  // Retrieve KOID of focus chain's root view.
  // Return ZX_KOID_INVALID if scene does not exist, or if the focus chain is empty.
  zx_koid_t focus_chain_root() const;

  // Request a focus change in the SceneGraph's ViewTree.
  //
  // The request is performed with the authority of the focus chain's root view (typically the
  // Scene). However, a request may be denied if the requested view may not receive focus (a
  // property set by the view holder).
  void RequestFocusChange(zx_koid_t view_ref_koid);

  // Checks if an accessibility listener is intercepting pointer events. If the
  // listener is on, initializes the buffer if it hasn't been created.
  // Important:
  // When the buffer is initialized, it can be the case that there are active
  // pointer event streams that haven't finished yet. They are sent to clients,
  // and *not* to the a11y listener. When the stream is done and a new stream
  // arrives, these will be sent to the a11y listener who will just continue its
  // normal flow. In a disconnection, if there are active pointer event streams,
  // its assume that the listener rejected them so they are sent to clients.
  bool ShouldForwardAccessibilityPointerEvents();

  // FIELDS

  gfx::Engine* const engine_ = nullptr;
  InputSystem* const input_system_ = nullptr;

  // Tracks the set of Views each touch event is delivered to; basically, a map from pointer ID to a
  // stack of ViewRef KOIDs. This is used to ensure consistent delivery of pointer events for a
  // given finger to its original destination targets on their respective DOWN event.  In
  // particular, a focus change triggered by a new finger should *not* affect delivery of events to
  // existing fingers.
  //
  // NOTE: We assume there is one touch screen, and hence unique pointer IDs.
  std::unordered_map<uint32_t, ViewStack> touch_targets_;

  // Tracks the View each mouse pointer is delivered to; a map from device ID to a ViewRef KOID.
  // This is used to ensure consistent delivery of mouse events for a given device.  A focus change
  // triggered by other pointer events should *not* affect delivery of events to existing mice.
  //
  // NOTE: We reuse the ViewStack here just for convenience.
  std::unordered_map<uint32_t, ViewStack> mouse_targets_;

  // TODO(SCN-1047): Remove when gesture disambiguation is the default.
  bool parallel_dispatch_ = true;

  // When accessibility pointer event forwarding is enabled, this buffer stores
  // pointer events until an accessibility listener decides how to handle them.
  // It is always null otherwise.
  std::unique_ptr<PointerEventBuffer> pointer_event_buffer_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_INPUT_COMMAND_DISPATCHER_H_
