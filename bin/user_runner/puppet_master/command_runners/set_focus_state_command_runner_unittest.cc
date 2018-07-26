// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_focus_state_command_runner.h"

#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"

namespace modular {
namespace {

class FocusHandler : fuchsia::modular::FocusProvider {
 public:
  FocusHandler() {}

  void AddProviderBinding(
      fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) {
    provider_bindings_.AddBinding(this, std::move(request));
  };

  std::string request_called_with_story_id() {
    return request_called_with_story_id_;
  }

  // |fuchsia::modular::FocusProvider|
  void Request(fidl::StringPtr story_id) override {
    request_called_with_story_id_ = story_id;
  };
  void Query(QueryCallback callback) override{};
  void Watch(
      fidl::InterfaceHandle<fuchsia::modular::FocusWatcher> watcher) override{};
  void Duplicate(fidl::InterfaceRequest<fuchsia::modular::FocusProvider>
                     request) override{};

 private:
  std::string request_called_with_story_id_;
  fidl::BindingSet<fuchsia::modular::FocusProvider> provider_bindings_;
};

class SetFocusStateCommandRunnerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    fidl::InterfacePtr<fuchsia::modular::FocusProvider> focus_provider;
    focus_handler_.AddProviderBinding(focus_provider.NewRequest());
    runner_ = std::make_unique<SetFocusStateCommandRunner>(
        std::move(focus_provider));
  }

 protected:
  FocusHandler focus_handler_;
  std::unique_ptr<SetFocusStateCommandRunner> runner_;
};

TEST_F(SetFocusStateCommandRunnerTest, Focus) {
  fuchsia::modular::SetFocusState set_focus_state;
  set_focus_state.focused = true;
  fuchsia::modular::StoryCommand command;
  command.set_set_focus_state(std::move(set_focus_state));

  runner_->Execute("story1", nullptr /* story_storage */, std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                               result.status);
                     EXPECT_EQ("story1", result.story_id);
                   });

  RunLoopUntilIdle();
  EXPECT_EQ("story1", focus_handler_.request_called_with_story_id());
}

TEST_F(SetFocusStateCommandRunnerTest, Unfocus) {
  fuchsia::modular::SetFocusState set_focus_state;
  set_focus_state.focused = false;
  fuchsia::modular::StoryCommand command;
  command.set_set_focus_state(std::move(set_focus_state));

  runner_->Execute(nullptr /* story_id */, nullptr /* story_storage */, std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                               result.status);
                     EXPECT_TRUE(result.story_id->empty());
                   });

  RunLoopUntilIdle();
  EXPECT_TRUE(focus_handler_.request_called_with_story_id().empty());
}

}  // namespace
}  // namespace modular
