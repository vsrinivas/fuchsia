// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>

#include <gtest/gtest.h>
#include <src/lib/vulkan/imagepipe_view/imagepipe_view.h>

#include "sdk/lib/ui/scenic/cpp/view_ref_pair.h"

namespace {

static constexpr fuchsia::ui::gfx::ViewProperties kViewProperties = {
    .bounding_box = {.max = {.x = 100, .y = 50}, .min = {.x = 10, .y = 5}}};

class MockSession : public fuchsia::ui::scenic::testing::Session_TestBase {
 public:
  MockSession() : binding_(this) {}

  void NotImplemented_(const std::string& name) override {}

  void Enqueue(::std::vector<fuchsia::ui::scenic::Command> cmds) override {
    for (auto& cmd : cmds) {
      if (cmd.is_gfx() && cmd.gfx().is_create_resource() &&
          cmd.gfx().create_resource().resource.is_shape_node()) {
        SendViewPropertiesChangedEvent();
      }
    }
  }

  void SendViewPropertiesChangedEvent() {
    fuchsia::ui::gfx::ViewPropertiesChangedEvent view_properties_changed_event = {
        .view_id = 0,
        .properties = kViewProperties,
    };
    fuchsia::ui::gfx::Event event;
    event.set_view_properties_changed(view_properties_changed_event);

    fuchsia::ui::scenic::Event scenic_event;
    scenic_event.set_gfx(std::move(event));

    std::vector<fuchsia::ui::scenic::Event> events;
    events.emplace_back(std::move(scenic_event));

    listener_->OnScenicEvent(std::move(events));
  }

  void Bind(fidl::InterfaceRequest<::fuchsia::ui::scenic::Session> request,
            ::fuchsia::ui::scenic::SessionListenerPtr listener) {
    binding_.Bind(std::move(request));
    listener_ = std::move(listener);
  }

 private:
  fidl::Binding<fuchsia::ui::scenic::Session> binding_;
  fuchsia::ui::scenic::SessionListenerPtr listener_;
};

class FakeScenic : public fuchsia::ui::scenic::testing::Scenic_TestBase {
 public:
  void NotImplemented_(const std::string& name) override {}

  void CreateSession(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) override {
    mock_session_.Bind(std::move(session), listener.Bind());
  }

  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

 private:
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
  MockSession mock_session_;
};

}  // namespace

class ImagePipeViewTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    fake_scenic_ = std::make_unique<FakeScenic>();
    provider_.service_directory_provider()->AddService(fake_scenic_->GetHandler());
  }

  static void OnViewPropertiesChanged(ImagePipeView& view,
                                      fuchsia::ui::gfx::ViewProperties view_properties) {
    view.OnViewPropertiesChanged(std::move(view_properties));
  }

  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<FakeScenic> fake_scenic_;
  std::unique_ptr<ImagePipeView> view_;
  float width_ = 0;
  float height_ = 0;
};

TEST_F(ImagePipeViewTest, Initialize) {
  ImagePipeView::ResizeCallback resize_callback = [this](float width, float height) {
    width_ = width;
    height_ = height;
  };

  zx::eventpair view_token_0, view_token_1;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &view_token_0, &view_token_1));

  auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
  auto view = ImagePipeView::Create(
      provider_.context(), scenic::ToViewToken(std::move(view_token_0)),
      std::move(view_ref_control), std::move(view_ref), std::move(resize_callback));
  ASSERT_TRUE(view);

  EXPECT_EQ(0.0, width_);
  EXPECT_EQ(0.0, height_);

  RunLoopUntilIdle();

  EXPECT_EQ(kViewProperties.bounding_box.max.x - kViewProperties.bounding_box.min.x, width_);
  EXPECT_EQ(kViewProperties.bounding_box.max.y - kViewProperties.bounding_box.min.y, height_);
}
