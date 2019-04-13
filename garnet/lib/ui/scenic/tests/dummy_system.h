// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_DUMMY_SYSTEM_H_
#define GARNET_LIB_UI_SCENIC_TESTS_DUMMY_SYSTEM_H_

#include "garnet/lib/ui/scenic/system.h"

namespace scenic_impl {
namespace test {

class DummySystem : public System {
 public:
  static constexpr TypeId kTypeId = kDummySystem;
  static const char* kName;

  explicit DummySystem(SystemContext context,
                       bool initialized_after_construction = true);
  ~DummySystem() override;

  CommandDispatcherUniquePtr CreateCommandDispatcher(
      CommandDispatcherContext context) override;

 private:
};

class DummyCommandDispatcher : public CommandDispatcher {
 public:
  explicit DummyCommandDispatcher(CommandDispatcherContext context);
  ~DummyCommandDispatcher() override;

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command) override;
};

}  // namespace test
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_SCENIC_TESTS_DUMMY_SYSTEM_H_
