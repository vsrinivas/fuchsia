// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_manager.h"

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
      zx_koid_t koid, fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
      vfs::PseudoDir* debug_dir,
      a11y::SemanticTreeService::CloseChannelCallback close_channel_callback) override {
    auto service = a11y::SemanticTreeServiceFactory::NewService(
        koid, std::move(semantic_listener), debug_dir, std::move(close_channel_callback));
    service_ = service.get();
    return service;
  }

  a11y::SemanticTreeService* service() { return service_; }

 private:
  a11y::SemanticTreeService* service_ = nullptr;
};

class ViewManagerTest : public gtest::TestLoopFixture {
 public:
  ViewManagerTest()
      : factory_(std::make_unique<MockSemanticTreeServiceFactory>()),
        factory_ptr_(factory_.get()),
        view_manager_(std::move(factory_), debug_dir()) {
    syslog::InitLogger();
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSemanticTreeServiceFactory> factory_;
  MockSemanticTreeServiceFactory* factory_ptr_;
  a11y::ViewManager view_manager_;
};

TEST_F(ViewManagerTest, ProviderGetsNotifiedOfSemanticsEnabled) {
  // Enable Semantics Manager.
  view_manager_.SetSemanticsEnabled(true);
  MockSemanticProvider semantic_provider(&view_manager_);
  // Upon initialization, MockSemanticProvider calls RegisterViewForSemantics().
  // Ensure that it called the factory to instantiate a new service.
  EXPECT_TRUE(factory_ptr_->service());
  RunLoopUntilIdle();

  EXPECT_TRUE(semantic_provider.GetSemanticsEnabled());

  // Disable Semantics Manager.
  view_manager_.SetSemanticsEnabled(false);
  RunLoopUntilIdle();
  // Semantics Listener should get notified about Semantics manager disable.
  EXPECT_FALSE(semantic_provider.GetSemanticsEnabled());
}

TEST_F(ViewManagerTest, GetsTreeByKoid) {
  view_manager_.SetSemanticsEnabled(true);
  MockSemanticProvider semantic_provider(&view_manager_);
  RunLoopUntilIdle();
  const auto tree_weak_ptr = view_manager_.GetTreeByKoid(semantic_provider.koid());
  EXPECT_TRUE(tree_weak_ptr);
  EXPECT_EQ(tree_weak_ptr->Size(), 0u);
}

TEST_F(ViewManagerTest, ClosesChannel) {
  view_manager_.SetSemanticsEnabled(true);
  MockSemanticProvider semantic_provider(&view_manager_);
  RunLoopUntilIdle();
  const auto tree_weak_ptr = view_manager_.GetTreeByKoid(semantic_provider.koid());
  EXPECT_TRUE(tree_weak_ptr);
  EXPECT_EQ(tree_weak_ptr->Size(), 0u);
  // Forces the client to disconnect.
  semantic_provider.SendEventPairSignal();
  RunLoopUntilIdle();
  const auto invalid_tree_weak_ptr = view_manager_.GetTreeByKoid(semantic_provider.koid());
  EXPECT_FALSE(invalid_tree_weak_ptr);
}

// Tests that log file is removed when semantic tree service entry is removed from semantics
// manager.
TEST_F(ViewManagerTest, LogFileRemoved) {
  view_manager_.SetSemanticsEnabled(true);
  MockSemanticProvider semantic_provider(&view_manager_);
  RunLoopUntilIdle();
  const auto tree_weak_ptr = view_manager_.GetTreeByKoid(semantic_provider.koid());
  std::string debug_file = std::to_string(semantic_provider.koid());
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

TEST_F(ViewManagerTest, SemanticsSourceViewHasSemantics) {
  view_manager_.SetSemanticsEnabled(true);
  MockSemanticProvider semantic_provider(&view_manager_);
  RunLoopUntilIdle();
  a11y::SemanticsSource* semantics_source = &view_manager_;
  EXPECT_TRUE(semantics_source->ViewHasSemantics(a11y::GetKoid(semantic_provider.view_ref())));
  // Forces the client to disconnect.
  semantic_provider.SendEventPairSignal();
  RunLoopUntilIdle();
  EXPECT_FALSE(semantics_source->ViewHasSemantics(a11y::GetKoid(semantic_provider.view_ref())));
}

TEST_F(ViewManagerTest, SemanticsSourceViewRefClone) {
  view_manager_.SetSemanticsEnabled(true);
  MockSemanticProvider semantic_provider(&view_manager_);
  RunLoopUntilIdle();
  a11y::SemanticsSource* semantics_source = &view_manager_;
  auto view_ref_or_null =
      semantics_source->ViewRefClone(a11y::GetKoid(semantic_provider.view_ref()));
  EXPECT_EQ(a11y::GetKoid(semantic_provider.view_ref()), a11y::GetKoid(*view_ref_or_null));
  // Forces the client to disconnect.
  semantic_provider.SendEventPairSignal();
  RunLoopUntilIdle();
  // The view is not providing semantics anymore, so there is no return value.
  EXPECT_FALSE(semantics_source->ViewRefClone(a11y::GetKoid(semantic_provider.view_ref())));
}

}  // namespace accessibility_test
