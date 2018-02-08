// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_TESTS_DUMMY_SYSTEM_H_
#define GARNET_LIB_UI_MOZART_TESTS_DUMMY_SYSTEM_H_

#include "garnet/lib/ui/mozart/system.h"

namespace mz {
namespace test {

class DummySystem : public System {
 public:
  static constexpr TypeId kTypeId = kDummySystem;

  explicit DummySystem(SystemContext context);
  ~DummySystem() override;

  std::unique_ptr<CommandDispatcher> CreateCommandDispatcher(
      CommandDispatcherContext context) override;

 private:
};

class DummyCommandDispatcher : public CommandDispatcher {
 public:
  explicit DummyCommandDispatcher(CommandDispatcherContext context);
  ~DummyCommandDispatcher() override;

  bool ApplyCommand(const ui_mozart::CommandPtr& command) override;
};

}  // namespace test
}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_TESTS_DUMMY_SYSTEM_H_
