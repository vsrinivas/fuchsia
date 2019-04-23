// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/syscalls.h>

#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "gtest/gtest.h"
#include "lib/ui/gfx/util/time.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "public/lib/escher/test/gtest_vulkan.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class CompositorTest : public SessionTest {
 public:
  CompositorTest() {}

  std::unique_ptr<SessionForTest> CreateSession() override {
    SessionContext session_context = CreateBarebonesSessionContext();

    // Generate scene graph.
    scene_graph_ = std::make_unique<SceneGraph>();

    // Generate other parameters needed for session context.
    sys::testing::ComponentContextProvider context_provider_;
    auto app_context = context_provider_.TakeContext();
    time_stamper_ = std::make_unique<EventTimestamper>(app_context.get());
    resource_linker_ = std::make_unique<ResourceLinker>();
    view_linker_ = std::make_unique<ViewLinker>();

    // Apply to the session context;
    session_context.event_timestamper = time_stamper_.get();
    session_context.view_linker = view_linker_.get();
    session_context.resource_linker = resource_linker_.get();

    // Finally apply scene graph weak pointer.
    session_context.scene_graph = scene_graph_->GetWeakPtr();

    // Return session
    return std::make_unique<SessionForTest>(1, std::move(session_context), this,
                                            error_reporter());
  }

 private:
  std::unique_ptr<SceneGraph> scene_graph_;
  std::unique_ptr<EventTimestamper> time_stamper_;
  std::unique_ptr<ViewLinker> view_linker_;
  std::unique_ptr<ResourceLinker> resource_linker_;
};

TEST_F(CompositorTest, Validation) {
  const int CompositorId = 15;
  std::array<float, 3> preoffsets = {0, 0, 0};
  std::array<float, 9> matrix = {0.3, 0.6, 0.1, 0.3, 0.6, 0.1, 0.3, 0.6, 0.1};
  std::array<float, 3> postoffsets = {0, 0, 0};

  ASSERT_TRUE(Apply(scenic::NewCreateDisplayCompositorCmd(CompositorId)));

  ASSERT_TRUE(Apply(scenic::NewSetDisplayColorConversionCmdHACK(
      CompositorId, preoffsets, matrix, postoffsets)));

  Display* display = display_manager_->default_display();
  ASSERT_TRUE(display != nullptr);

  const ColorTransform& transform = display->color_transform();

  ASSERT_TRUE(transform.preoffsets == preoffsets);
  ASSERT_TRUE(transform.matrix == matrix);
  ASSERT_TRUE(transform.postoffsets == postoffsets);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
