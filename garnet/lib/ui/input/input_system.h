// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_
#define GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_

#include <fuchsia/ui/input/cpp/fidl.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/input/view_id.h"
#include "garnet/lib/ui/scenic/system.h"

namespace scenic_impl {
namespace input {

// Routes input events from a root presenter to Scenic clients.
// Manages input-related state, such as focus.
//
// The general flow of events is:
// DispatchCommand --[decide what/where]--> EnqueueEvent
class InputSystem : public System {
 public:
  static constexpr TypeId kTypeId = kInput;
  static const char* kName;

  explicit InputSystem(SystemContext context, gfx::GfxSystem* scenic);
  virtual ~InputSystem() = default;

  virtual CommandDispatcherUniquePtr CreateCommandDispatcher(
      CommandDispatcherContext context) override;

  fuchsia::ui::input::ImeServicePtr& text_sync_service() {
    return text_sync_service_;
  }

  std::unordered_set<SessionId>& hard_keyboard_requested() {
    return hard_keyboard_requested_;
  }

 private:
  gfx::GfxSystem* const gfx_system_;

  // Send hard keyboard events to Text Sync for dispatch via IME; this is the
  // intended flow for clients to receive *mediated* keyboard events.
  // The connection to Text Sync is shared between all dispatchers.
  fuchsia::ui::input::ImeServicePtr text_sync_service_;

  // By default, clients don't get hard keyboard events directly from Scenic.
  // Clients may request these events via the SetHardKeyboardDeliveryCmd;
  // this set remembers which sessions have opted in.  We need this map because
  // each InputCommandDispatcher works independently.
  std::unordered_set<SessionId> hard_keyboard_requested_;
};

// Per-session treatment of input commands.
class InputCommandDispatcher : public CommandDispatcher {
 public:
  InputCommandDispatcher(CommandDispatcherContext context,
                         gfx::GfxSystem* gfx_system, InputSystem* input_system);
  ~InputCommandDispatcher() override = default;

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command) override;

 private:
  // Per-command dispatch logic.
  void DispatchCommand(const fuchsia::ui::input::SendPointerInputCmd command);
  void DispatchCommand(const fuchsia::ui::input::SendKeyboardInputCmd command);
  void DispatchCommand(
      const fuchsia::ui::input::SetHardKeyboardDeliveryCmd command);
  void DispatchCommand(
      const fuchsia::ui::input::SetParallelDispatchCmd command);

  // Per-pointer-type dispatch logic.
  void DispatchTouchCommand(
      const fuchsia::ui::input::SendPointerInputCmd command);
  void DispatchMouseCommand(
      const fuchsia::ui::input::SendPointerInputCmd command);

  // Enqueue the focus event into the view's SessionListener.
  void EnqueueEventToView(GlobalId view_id,
                          fuchsia::ui::input::FocusEvent focus);

  // Enqueue the pointer event into the view's SessionListener.
  void EnqueueEventToView(GlobalId view_id,
                          fuchsia::ui::input::PointerEvent pointer);

  // Enqueue the keyboard event into the view's SessionListener.
  void EnqueueEventToView(GlobalId view_id,
                          fuchsia::ui::input::KeyboardEvent keyboard);

  // Enqueue the keyboard event to the Text Sync service.
  void EnqueueEventToTextSync(GlobalId view_id,
                              fuchsia::ui::input::KeyboardEvent keyboard);

  // FIELDS

  gfx::GfxSystem* const gfx_system_ = nullptr;
  InputSystem* const input_system_ = nullptr;

  // Tracks which View has focus.
  GlobalId focus_;

  // Tracks the set of Views each touch event is delivered to; a map from
  // pointer ID to a stack of GlobalIds. This is used to ensure consistent
  // delivery of pointer events for a given finger to its original destination
  // targets on their respective DOWN event. In particular, a focus change
  // triggered by a new finger should *not* affect delivery of events to
  // existing fingers.
  //
  // NOTE: We assume there is one touch screen, and hence unique pointer IDs.
  std::unordered_map<uint32_t, ViewStack> touch_targets_;

  // Tracks the View each mouse pointer is delivered to; a map from device ID to
  // a GlobalId. This is used to ensure consistent delivery of mouse events for
  // a given device. A focus change triggered by other pointer events should
  // *not* affect delivery of events to existing mice.
  //
  // NOTE: We reuse the ViewStack here just for convenience.
  std::unordered_map<uint32_t, ViewStack> mouse_targets_;

  // TODO(SCN-1047): Remove when gesture disambiguation is the default.
  bool parallel_dispatch_ = true;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_
