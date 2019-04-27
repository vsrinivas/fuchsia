// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <memory>
#include <string>
#include <vector>

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/hit.h"
#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "garnet/lib/ui/scenic/util/print_event.h"
#include "gtest/gtest.h"
#include "lib/escher/forward_declarations.h"
#include "lib/fostr/fidl/fuchsia/ui/scenic/formatting.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"
#include "src/lib/fxl/logging.h"

// The test setup here is sufficiently different from hittest_unittest.cc to
// merit its own file.  We access the global hit test through the compositor,
// instead of through a session.

namespace scenic_impl {
namespace gfx {
namespace test {

// Session wrapper that references a common Engine.
//
// This class calls Session::TearDown directly and so avoids pulling in
// SessionHandler and SessionManager; these make a call to TearDown that's
// triggered by Engine::RenderFrame, and we don't need that here.
class CustomSession {
 public:
  CustomSession(SessionId id, SessionContext session_context) {
    session_ = std::make_unique<SessionForTest>(id, std::move(session_context));
  }

  ~CustomSession() {}

  void Apply(::fuchsia::ui::gfx::Command command) {
    CommandContext empty_command_context(nullptr);
    bool result =
        session_->ApplyCommand(&empty_command_context, std::move(command));
    ASSERT_TRUE(result) << "Failed to apply: " << command;  // Fail fast.
  }

 private:
  std::unique_ptr<SessionForTest> session_;
};

// Loop fixture provides dispatcher for Engine's EventTimestamper.
using MultiSessionHitTestTest = ::gtest::TestLoopFixture;

// A comprehensive test that sets up three independent sessions, with
// View/ViewHolder pairs, and checks if global hit testing has access to
// hittable nodes across all sessions.
TEST_F(MultiSessionHitTestTest, GlobalHits) {
  DisplayManager display_manager;
  display_manager.SetDefaultDisplayForTests(std::make_unique<Display>(
      /*id*/ 0, /*px-width*/ 9, /*px-height*/ 9));
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<Engine> engine = std::make_unique<EngineForTest>(
      context_provider_.context(), &display_manager,
      /*release fence signaller*/ nullptr);

  // Create our tokens for View/ViewHolder creation.
  auto [view_token_1, view_holder_token_1] = scenic::ViewTokenPair::New();
  auto [view_token_2, view_holder_token_2] = scenic::ViewTokenPair::New();

  // Root session sets up the scene and two view holders.
  CustomSession s_r(0, engine->session_context());
  {
    const uint32_t kCompositorId = 1001;
    const uint32_t kLayerStackId = 1002;
    const uint32_t kLayerId = 1003;
    s_r.Apply(scenic::NewCreateCompositorCmd(kCompositorId));
    s_r.Apply(scenic::NewCreateLayerStackCmd(kLayerStackId));
    s_r.Apply(scenic::NewSetLayerStackCmd(kCompositorId, kLayerStackId));
    s_r.Apply(scenic::NewCreateLayerCmd(kLayerId));
    s_r.Apply(scenic::NewSetSizeCmd(
        kLayerId, (float[2]){/*px-width*/ 9, /*px-height*/ 9}));
    s_r.Apply(scenic::NewAddLayerCmd(kLayerStackId, kLayerId));

    const uint32_t kSceneId = 1004;  // Hit
    const uint32_t kCameraId = 1005;
    const uint32_t kRendererId = 1006;
    s_r.Apply(scenic::NewCreateSceneCmd(kSceneId));
    s_r.Apply(scenic::NewCreateCameraCmd(kCameraId, kSceneId));
    s_r.Apply(scenic::NewCreateRendererCmd(kRendererId));
    s_r.Apply(scenic::NewSetCameraCmd(kRendererId, kCameraId));
    s_r.Apply(scenic::NewSetRendererCmd(kLayerId, kRendererId));

    // TODO(SCN-885) - Adjust hit count; an EntityNode shouldn't be hit.
    const uint32_t kRootNodeId = 1007;  // Hit
    s_r.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));

    const uint32_t kViewHolder1Id = 1008;  // Hit
    s_r.Apply(scenic::NewAddChildCmd(kSceneId, kRootNodeId));
    s_r.Apply(scenic::NewCreateViewHolderCmd(
        kViewHolder1Id, std::move(view_holder_token_1), "viewholder_1"));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolder1Id));

    const uint32_t kViewHolder2Id = 1009;  // Hit
    s_r.Apply(scenic::NewCreateViewHolderCmd(
        kViewHolder2Id, std::move(view_holder_token_2), "viewholder_2"));
    s_r.Apply(scenic::NewAddChildCmd(kRootNodeId, kViewHolder2Id));
  }

  // Two sessions (s_1 and s_2) create an overlapping and hittable surface.
  CustomSession s_1(1, engine->session_context());
  {
    const uint32_t kViewId = 2001;  // Hit
    s_1.Apply(
        scenic::NewCreateViewCmd(kViewId, std::move(view_token_1), "view_1"));

    const uint32_t kRootNodeId = 2002;  // Hit
    s_1.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_1.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    const uint32_t kChildId = 2003;  // Hit
    s_1.Apply(scenic::NewCreateShapeNodeCmd(kChildId));
    s_1.Apply(scenic::NewAddChildCmd(kRootNodeId, kChildId));
    s_1.Apply(scenic::NewSetTranslationCmd(kChildId,
                                           (float[3]){4.f, 4.f, /*z*/ -2.f}));

    const uint32_t kShapeId = 2004;
    s_1.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_1.Apply(scenic::NewSetShapeCmd(kChildId, kShapeId));
  }

  CustomSession s_2(2, engine->session_context());
  {
    const uint32_t kViewId = 3001;  // Hit
    s_2.Apply(
        scenic::NewCreateViewCmd(kViewId, std::move(view_token_2), "view_2"));

    const uint32_t kRootNodeId = 3002;  // Hit
    s_2.Apply(scenic::NewCreateEntityNodeCmd(kRootNodeId));
    s_2.Apply(scenic::NewAddChildCmd(kViewId, kRootNodeId));

    const uint32_t kChildId = 3003;  // Hit
    s_2.Apply(scenic::NewCreateShapeNodeCmd(kChildId));
    s_2.Apply(scenic::NewAddChildCmd(kRootNodeId, kChildId));
    s_2.Apply(scenic::NewSetTranslationCmd(kChildId,
                                           (float[3]){4.f, 4.f, /*z*/ -3.f}));

    const uint32_t kShapeId = 3004;
    s_2.Apply(scenic::NewCreateRectangleCmd(kShapeId, /*px-width*/ 9.f,
                                            /*px-height*/ 9.f));
    s_2.Apply(scenic::NewSetShapeCmd(kChildId, kShapeId));
  }

#if 0
  FXL_LOG(INFO) << engine->DumpScenes();  // Handy debugging.
#endif

  std::vector<Hit> hits;
  {
    // Models input subsystem's access to Engine internals.
    // For simplicity, we use the first (and only) compositor and layer stack.
    const CompositorWeakPtr& compositor =
        engine->scene_graph()->first_compositor();
    ASSERT_TRUE(compositor);
    LayerStackPtr layer_stack = compositor->layer_stack();
    ASSERT_NE(layer_stack.get(), nullptr);

    escher::ray4 ray;
    ray.origin = escher::vec4(4.f, 4.f, 1.f, 1.f);
    ray.direction = escher::vec4(0.f, 0.f, -1.f, 0.f);
    GlobalHitTester hit_tester;
    hits = layer_stack->HitTest(ray, &hit_tester);
  }

  // All that for this!
  EXPECT_EQ(hits.size(), 10u) << "Should see ten hits across three sessions.";
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
