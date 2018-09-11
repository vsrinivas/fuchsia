// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_
#define GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_

#include <memory>
#include <unordered_map>

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/input/view_id.h"
#include "garnet/lib/ui/scenic/system.h"

namespace scenic_impl {
namespace input {

// Routes input events from a root presenter to Scenic clients.
// Manages input-related state, such as focus.
class InputSystem : public System {
 public:
  static constexpr TypeId kTypeId = kInput;

  explicit InputSystem(SystemContext context, gfx::GfxSystem* scenic);
  virtual ~InputSystem() = default;

  virtual std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) override;

 private:
  gfx::GfxSystem* const gfx_system_;
};

class InputCommandDispatcher : public CommandDispatcher {
 public:
  InputCommandDispatcher(CommandDispatcherContext context,
                         gfx::GfxSystem* scenic_system);
  ~InputCommandDispatcher() override = default;

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command) override;

 private:
  // Retrieve ViewPtr given its SessionId and ResourceId. If either is not
  // available, return nullptr.
  gfx::ViewPtr FindView(ViewId view_id);

  // Enqueue the focus event into the view's SessionListener.
  void EnqueueEventToView(gfx::ViewPtr view,
                          fuchsia::ui::input::FocusEvent focus);

  // Enqueue the keyboard event into the view's SessionListener.
  void EnqueueEventToView(gfx::ViewPtr view,
                          fuchsia::ui::input::KeyboardEvent keyboard);

  // Enqueue the pointer event into the view's SessionListener.
  void EnqueueEventToView(gfx::ViewPtr view,
                          fuchsia::ui::input::PointerEvent pointer);

  gfx::GfxSystem* const gfx_system_ = nullptr;

  // Tracks which View has focus.
  ViewId focus_;

  // Tracks the set of Views each pointer is delivered to; a map from pointer ID
  // to a stack of ViewIds. This is used to ensure consistent delivery of
  // pointer events for a given finger to its original destination targets on
  // their respective DOWN event. In particular, changes in focus from a new
  // finger should *not* affect delivery of events for existing fingers.
  std::unordered_map<uint32_t, ViewStack> pointer_targets_;
};

}  // namespace input
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_
