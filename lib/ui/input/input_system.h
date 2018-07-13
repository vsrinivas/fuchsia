// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_
#define GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_

#include <memory>

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/input/focus.h"
#include "garnet/lib/ui/scenic/system.h"

namespace scenic {
namespace input {

// Routes input events from a root presenter to Scenic clients.
// Manages input-related state, such as Focus.
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
  ~InputCommandDispatcher() override;

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command) override;

 private:
  // Retrieve ViewPtr given its SessionId and ResourceId. If either is not
  // available, return nullptr.
  gfx::ViewPtr FindView(ViewId view_id);

  // Enqueue the focus event into the view's SessionListener.
  void EnqueueEvent(gfx::ViewPtr view, fuchsia::ui::input::FocusEvent focus);

  gfx::GfxSystem* const gfx_system_ = nullptr;

  std::unique_ptr<Focus> focus_;
};

}  // namespace input
}  // namespace scenic

#endif  // GARNET_LIB_UI_INPUT_INPUT_SYSTEM_H_
