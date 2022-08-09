// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl_test_base.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>

#include <gtest/gtest.h>
#include <src/lib/vulkan/flatland_view/flatland_view.h>

#include "lib/zx/time.h"
#include "sdk/lib/ui/scenic/cpp/view_creation_tokens.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {

static const uint32_t kWidth = 100;
static const uint32_t kHeight = 50;

class FakeFlatland : public fuchsia::ui::composition::testing::Flatland_TestBase,
                     public fuchsia::ui::composition::testing::ParentViewportWatcher_TestBase {
 public:
  void NotImplemented_(const std::string& name) override {}

  fidl::InterfaceRequestHandler<fuchsia::ui::composition::Flatland> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::ui::composition::Flatland> request) {
      flatland_bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  // |fuchsia::ui::composition::Flatland|
  void CreateView2(fuchsia::ui::views::ViewCreationToken token,
                   fuchsia::ui::views::ViewIdentityOnCreation view_identity,
                   fuchsia::ui::composition::ViewBoundProtocols view_protocols,
                   fidl::InterfaceRequest<fuchsia::ui::composition::ParentViewportWatcher>
                       parent_viewport_watcher) override {
    parent_viewport_watcher_bindings_.AddBinding(this, std::move(parent_viewport_watcher));
  }

  // |fuchsia::ui::composition::ParentViewportWatcher|
  void GetLayout(GetLayoutCallback callback) override {
    fuchsia::ui::composition::LayoutInfo info;
    info.mutable_logical_size()->width = kWidth;
    info.mutable_logical_size()->height = kHeight;
    callback(std::move(info));
  }

 private:
  fidl::BindingSet<fuchsia::ui::composition::Flatland> flatland_bindings_;
  fidl::BindingSet<fuchsia::ui::composition::ParentViewportWatcher>
      parent_viewport_watcher_bindings_;
};

}  // namespace

class FlatlandViewTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    fake_flatland_ = std::make_unique<FakeFlatland>();
    provider_.service_directory_provider()->AddService(fake_flatland_->GetHandler());
  }

  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<FakeFlatland> fake_flatland_;
  std::unique_ptr<FlatlandView> view_;
  float width_ = 0;
  float height_ = 0;
};

TEST_F(FlatlandViewTest, Initialize) {
  FlatlandView::ResizeCallback resize_callback = [this](float width, float height) {
    width_ = width;
    height_ = height;
    QuitLoop();
  };

  zx::eventpair view_token_0, view_token_1;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_token_0, &view_token_1));

  auto [view_token, viewport_token] = scenic::ViewCreationTokenPair::New();
  auto view =
      FlatlandView::Create(provider_.context(), std::move(view_token), std::move(resize_callback));
  ASSERT_TRUE(view);

  EXPECT_EQ(0.0, width_);
  EXPECT_EQ(0.0, height_);
  RunLoopFor(zx::sec(3));

  EXPECT_EQ(kWidth, width_);
  EXPECT_EQ(kHeight, height_);
}
