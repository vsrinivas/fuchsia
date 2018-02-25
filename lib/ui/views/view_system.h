// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_VIEWS_VIEW_SYSTEM_H_
#define GARNET_LIB_UI_VIEWS_VIEW_SYSTEM_H_

#include "garnet/lib/ui/mozart/system.h"
#include "garnet/lib/ui/scenic/scenic_system.h"

namespace mz {

// TODO(MZ-552): document.
class ViewSystem : public System {
 public:
  static constexpr TypeId kTypeId = kViews;

  explicit ViewSystem(mz::SystemContext context,
                      scene_manager::ScenicSystem* scenic);
  ~ViewSystem() override;

  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      mz::CommandDispatcherContext context) override;

 private:
  scene_manager::ScenicSystem* scenic_system_;
};

// TODO(MZ-552): document.
class ViewCommandDispatcher : public CommandDispatcher {
 public:
  ViewCommandDispatcher(mz::CommandDispatcherContext context,
                        scene_manager::ScenicSystem* scenic_system);
  ~ViewCommandDispatcher() override;

  bool ApplyCommand(const ui_mozart::CommandPtr& command) override;

 private:
  scene_manager::ScenicSystem* scenic_system_;
};

}  // namespace mz

#endif  // GARNET_LIB_UI_VIEWS_VIEW_SYSTEM_H_
