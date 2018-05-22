// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SKETCHY_SKETCHY_SYSTEM_H_
#define GARNET_LIB_UI_SKETCHY_SKETCHY_SYSTEM_H_

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/scenic/system.h"

namespace scenic {

// TODO(MZ-552): document.
class SketchySystem : public System {
 public:
  static constexpr TypeId kTypeId = kSketchy;

  SketchySystem(SystemContext context, gfx::GfxSystem* scenic);
  ~SketchySystem() override;

  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) override;

 private:
  gfx::GfxSystem* const gfx_system_;
};

// TODO(MZ-552): document.
class SketchyCommandDispatcher : public CommandDispatcher {
 public:
  SketchyCommandDispatcher(CommandDispatcherContext context,
                           gfx::GfxSystem* scenic);
  ~SketchyCommandDispatcher() override;

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command) override;

 private:
  gfx::GfxSystem* const gfx_system_;
};

}  // namespace scenic

#endif  // GARNET_LIB_UI_SKETCHY_SKETCHY_SYSTEM_H_
