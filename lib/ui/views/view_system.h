// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_VIEWS_VIEW_SYSTEM_H_
#define GARNET_LIB_UI_VIEWS_VIEW_SYSTEM_H_

#include "garnet/lib/ui/gfx/scenic_system.h"
#include "garnet/lib/ui/scenic/system.h"

namespace mz {

// TODO(MZ-552): document.
class ViewSystem : public System {
 public:
  static constexpr TypeId kTypeId = kViews;

  explicit ViewSystem(mz::SystemContext context,
                      scenic::gfx::ScenicSystem* scenic);
  ~ViewSystem() override;

  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      mz::CommandDispatcherContext context) override;

 private:
  scenic::gfx::ScenicSystem* scenic_system_;
};

// TODO(MZ-552): document.
class ViewCommandDispatcher : public CommandDispatcher {
 public:
  ViewCommandDispatcher(mz::CommandDispatcherContext context,
                        scenic::gfx::ScenicSystem* scenic_system);
  ~ViewCommandDispatcher() override;

  bool ApplyCommand(const ui::CommandPtr& command) override;

 private:
  scenic::gfx::ScenicSystem* scenic_system_;
};

}  // namespace mz

#endif  // GARNET_LIB_UI_VIEWS_VIEW_SYSTEM_H_
