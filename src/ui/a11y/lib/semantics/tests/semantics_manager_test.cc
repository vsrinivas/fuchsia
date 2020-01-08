// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/semantics_manager.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/zx/event.h>

#include <vector>

#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/semantic_tree_parser.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::NodePtr;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::semantics::SemanticsManager;

class MockSemanticTreeServiceFactory : public a11y::SemanticTreeServiceFactory {
 public:
  std::unique_ptr<a11y::SemanticTreeService> NewService(
      fuchsia::ui::views::ViewRef view_ref,
      fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
      vfs::PseudoDir* debug_dir,
      a11y::SemanticTreeService::CloseChannelCallback close_channel_callback) override {
    auto service = a11y::SemanticTreeServiceFactory::NewService(
        std::move(view_ref), std::move(semantic_listener), debug_dir,
        std::move(close_channel_callback));
    service_ = service.get();
    return service;
  }

  a11y::SemanticTreeService* service() { return service_; }

 private:
  a11y::SemanticTreeService* service_ = nullptr;
};

class SemanticsManagerTest : public gtest::TestLoopFixture {
 public:
  SemanticsManagerTest()
      : factory_(std::make_unique<MockSemanticTreeServiceFactory>()),
        factory_ptr_(factory_.get()),
        semantics_manager_(std::move(factory_), debug_dir()) {
    syslog::InitLogger();
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSemanticTreeServiceFactory> factory_;
  MockSemanticTreeServiceFactory* factory_ptr_;
  a11y::SemanticsManager semantics_manager_;
};

TEST_F(SemanticsManagerTest, ProviderGetsNotifiedOfSemanticsEnabled) {
  // Enable Semantics Manager.
  semantics_manager_.SetSemanticsManagerEnabled(true);
  MockSemanticProvider semantic_provider(&semantics_manager_);
  // Upon initialization, MockSemanticProvider calls RegisterViewForSemantics().
  // Ensure that it called the factory to instantiate a new service.
  EXPECT_TRUE(factory_ptr_->service());
  RunLoopUntilIdle();

  EXPECT_TRUE(semantic_provider.GetSemanticsEnabled());

  // Disable Semantics Manager.
  semantics_manager_.SetSemanticsManagerEnabled(false);
  RunLoopUntilIdle();
  // Semantics Listener should get notified about Semantics manager disable.
  EXPECT_FALSE(semantic_provider.GetSemanticsEnabled());
}

TEST_F(SemanticsManagerTest, GetsTreeByKoid) {
  semantics_manager_.SetSemanticsManagerEnabled(true);
  MockSemanticProvider semantic_provider(&semantics_manager_);
  RunLoopUntilIdle();
  const auto tree_weak_ptr =
      semantics_manager_.GetTreeByKoid(a11y::GetKoid(semantic_provider.view_ref()));
  EXPECT_TRUE(tree_weak_ptr);
  EXPECT_EQ(tree_weak_ptr->Size(), 0u);
}

TEST_F(SemanticsManagerTest, ClosesChannel) {
  semantics_manager_.SetSemanticsManagerEnabled(true);
  MockSemanticProvider semantic_provider(&semantics_manager_);
  RunLoopUntilIdle();
  const auto tree_weak_ptr =
      semantics_manager_.GetTreeByKoid(a11y::GetKoid(semantic_provider.view_ref()));
  EXPECT_TRUE(tree_weak_ptr);
  EXPECT_EQ(tree_weak_ptr->Size(), 0u);
  // Forces the client to disconnect.
  semantic_provider.SendEventPairSignal();
  RunLoopUntilIdle();
  const auto invalid_tree_weak_ptr =
      semantics_manager_.GetTreeByKoid(a11y::GetKoid(semantic_provider.view_ref()));
  EXPECT_FALSE(invalid_tree_weak_ptr);
}

// Tests that log file is removed when semantic tree service entry is removed from semantics
// manager.
TEST_F(SemanticsManagerTest, LogFileRemoved) {
  semantics_manager_.SetSemanticsManagerEnabled(true);
  MockSemanticProvider semantic_provider(&semantics_manager_);
  RunLoopUntilIdle();
  const auto tree_weak_ptr =
      semantics_manager_.GetTreeByKoid(a11y::GetKoid(semantic_provider.view_ref()));
  std::string debug_file = std::to_string(a11y::GetKoid(semantic_provider.view_ref()));
  {
    vfs::internal::Node* node;
    EXPECT_EQ(ZX_OK, debug_dir()->Lookup(debug_file, &node));
  }

  // Forces the client to disconnect.
  semantic_provider.SendEventPairSignal();
  RunLoopUntilIdle();

  // Check Log File is removed.
  {
    vfs::internal::Node* node;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, debug_dir()->Lookup(debug_file, &node));
  }
}

}  // namespace accessibility_test
