// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <zircon/syscalls.h>

#include <thread>

#include "gtest/gtest.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/scenic/lib/gfx/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/gfx/util/time.h"

namespace scenic_impl {
namespace gfx {
namespace test {
struct ChannelPair {
  zx::channel server;
  zx::channel client;
};

ChannelPair CreateChannelPair() {
  ChannelPair c;
  FXL_CHECK(ZX_OK == zx::channel::create(0, &c.server, &c.client));
  return c;
}
class CompositorTest : public SessionTest {
 public:
  CompositorTest() {}

  void SetUp() {
    SessionTest::SetUp();
    display_manager_ = std::make_unique<DisplayManager>();

    constexpr float display_width = 1024;
    constexpr float display_height = 768;
    display_manager_->SetDefaultDisplayForTests(std::make_unique<Display>(
        /*id*/ 0, /*px-width*/ display_width, /*px-height*/ display_height));
    sysmem_ = std::make_unique<Sysmem>();
  }

  void TearDown() override {
    SessionTest::TearDown();

    view_linker_.reset();
    resource_linker_.reset();
    scene_graph_.reset();
    display_manager_.reset();
    sysmem_.reset();
  }

  SessionContext CreateSessionContext() override {
    SessionContext session_context = SessionTest::CreateSessionContext();

    FXL_DCHECK(!scene_graph_);
    FXL_DCHECK(!resource_linker_);
    FXL_DCHECK(!view_linker_);

    // Generate scene graph.
    scene_graph_ = std::make_unique<SceneGraph>(context_provider_.context());

    // Generate other parameters needed for session context.
    resource_linker_ = std::make_unique<ResourceLinker>();
    view_linker_ = std::make_unique<ViewLinker>();

    ChannelPair device_channel = CreateChannelPair();
    ChannelPair controller_channel = CreateChannelPair();

    mock_display_controller_.Bind(std::move(device_channel.server),
                                  std::move(controller_channel.server));

    display_controller_.Bind(std::move(controller_channel.client));

    // Apply to the session context;
    session_context.view_linker = view_linker_.get();
    session_context.resource_linker = resource_linker_.get();

    // Finally apply scene graph weak pointer.
    session_context.scene_graph = scene_graph_->GetWeakPtr();

    // Return session
    return session_context;
  }

  CommandContext CreateCommandContext() {
    return CommandContext(/* batch_gpu_uploader */ nullptr, sysmem_.get(), display_manager_.get(),
                          scene_graph_->GetWeakPtr());
  }

  DisplayManager* display_manager() const { return display_manager_.get(); }

 protected:
  fidl::InterfaceHandle<fuchsia::hardware::display::Controller> controller;
  MockDisplayController mock_display_controller_;
  fuchsia::hardware::display::ControllerSyncPtr display_controller_;

 private:
  std::unique_ptr<Sysmem> sysmem_;
  std::unique_ptr<DisplayManager> display_manager_;
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<SceneGraph> scene_graph_;

  std::unique_ptr<ViewLinker> view_linker_;
  std::unique_ptr<ResourceLinker> resource_linker_;
};

TEST_F(CompositorTest, Validation) {
  const int CompositorId = 15;
  std::array<float, 3> preoffsets = {0, 0, 0};
  std::array<float, 9> matrix = {0.3, 0.6, 0.1, 0.3, 0.6, 0.1, 0.3, 0.6, 0.1};
  std::array<float, 3> postoffsets = {0, 0, 0};

  ASSERT_TRUE(Apply(scenic::NewCreateDisplayCompositorCmd(CompositorId)));

  ASSERT_TRUE(Apply(
      scenic::NewSetDisplayColorConversionCmdHACK(CompositorId, preoffsets, matrix, postoffsets)));

  Display* display = display_manager()->default_display();
  ASSERT_TRUE(display != nullptr);

  const ColorTransform& transform = display->color_transform();

  ASSERT_TRUE(transform.preoffsets == preoffsets);
  ASSERT_TRUE(transform.matrix == matrix);
  ASSERT_TRUE(transform.postoffsets == postoffsets);
}

TEST_F(CompositorTest, ColorConversionConfigChecking) {
  ColorTransform transform;
  Display* display = display_manager()->default_display();

  uint32_t check_config_call_count = 0;
  bool should_discard_config = false;
  auto check_config_fn = [&](bool discard, fuchsia::hardware::display::ConfigResult* result,
                            std::vector<fuchsia::hardware::display::ClientCompositionOp>* ops) {
    *result = fuchsia::hardware::display::ConfigResult::UNSUPPORTED_CONFIG;

    fuchsia::hardware::display::ClientCompositionOp op;
    op.opcode = fuchsia::hardware::display::ClientCompositionOpcode::CLIENT_COLOR_CONVERSION;
    ops->push_back(op);
    check_config_call_count++;
    if (discard) {
      should_discard_config = true;
    }
  };
  mock_display_controller_.set_check_config_fn(check_config_fn);

  std::thread client([display_controller = std::move(display_controller_), display,
                      transform]() mutable {
    DisplayManager::SetDisplayColorConversion(display, display_controller, transform);
  });

  // Wait for |SetDisplayColorConversion|.
  mock_display_controller_.WaitForMessage();
  
  // Wait for |CheckConfig|.
  mock_display_controller_.WaitForMessage();
  
  // Wait for |CheckConfig|.
  mock_display_controller_.WaitForMessage();

  client.join();

  // The function check_config_fn should be called twice, once for the
  // initial config check, and once with the |discard| variable set to true.
  EXPECT_EQ(check_config_call_count, 2U);
  EXPECT_TRUE(should_discard_config);
}
}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
