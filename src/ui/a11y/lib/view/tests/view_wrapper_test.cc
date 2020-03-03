// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {

class MockSemanticTreeService : public a11y::SemanticTreeService {
  void EnableSemanticsUpdates(bool enabled) { enabled_ = enabled; }

  bool UpdatesEnabled() { return enabled_; }

 private:
  bool enabled_ = false;
};

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

class ViewWrapperTest : public gtest::TestLoopFixture {
 public:
  ViewWrapperTest()
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
  fuchsia::accessibility::semantics::SemanticTreePtr tree_ptr_;
};

TEST_F(ViewWrapperTest, EnableSemantics) {
  fuchsia::ui::views::ViewRef view_ref;
  MockSemanticListener semantic_listener;
  fidl::Binding<fuchsia::accessibility::semantics::SemanticListener> semantic_listener_binding(
      &semantic_listener);
  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener_ptr;

  zx_koid_t koid = a11y::GetKoid(view_ref);
  std::string debug_file = std::to_string(koid);
  std::unique_ptr<a11y::SemanticTreeService> tree_service_ptr =
      factory_ptr_->NewService(koid, std::move(semantic_listener_ptr),
                               context_provider_.context()->outgoing()->debug_dir(), [] {});

  a11y::ViewWrapper view_wrapper(std::move(view_ref), std::move(tree_service_ptr),
                                 tree_ptr_.NewRequest());

  view_wrapper.EnableSemanticUpdates(true);
  EXPECT_TRUE(factory_ptr_->service());
  EXPECT_TRUE(factory_ptr_->service()->UpdatesEnabled());
}

}  // namespace accessibility_test
