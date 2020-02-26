// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/annotation/annotation_view.h"

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>

#include <vector>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace a11y {
namespace {

static constexpr fuchsia::ui::gfx::ViewProperties kViewProperties = {
    .bounding_box = {.min = {.x = 10, .y = 5}, .max = {.x = 100, .y = 50}}};

class MockAnnotationRegistry : public fuchsia::ui::annotation::Registry {
 public:
  MockAnnotationRegistry() = default;
  ~MockAnnotationRegistry() override = default;

  void CreateAnnotationViewHolder(
      fuchsia::ui::views::ViewRef client_view,
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fuchsia::ui::annotation::Registry::CreateAnnotationViewHolderCallback callback) override {
    create_annotation_view_holder_called_ = true;
  }

  fidl::InterfaceRequestHandler<fuchsia::ui::annotation::Registry> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::ui::annotation::Registry> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  bool create_annotation_view_holder_called() { return create_annotation_view_holder_called_; }

 private:
  fidl::BindingSet<fuchsia::ui::annotation::Registry> bindings_;
  bool create_annotation_view_holder_called_;
};

class MockSession : public fuchsia::ui::scenic::testing::Session_TestBase {
 public:
  MockSession() : binding_(this) {}

  void NotImplemented_(const std::string& name) override {}

  void Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) override {
    cmd_queue_.insert(cmd_queue_.end(), std::make_move_iterator(cmds.begin()),
                      std::make_move_iterator(cmds.end()));
  }

  void Present(uint64_t presentation_time, ::std::vector<::zx::event> acquire_fences,
               ::std::vector<::zx::event> release_fences, PresentCallback callback) override {
    presented_cmds_.insert(presented_cmds_.end(), std::make_move_iterator(cmd_queue_.begin()),
                           std::make_move_iterator(cmd_queue_.end()));
    cmd_queue_.clear();
    callback(fuchsia::images::PresentationInfo());
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

  const std::vector<fuchsia::ui::scenic::Command>& GetCommandQueue() { return cmd_queue_; }
  const std::vector<fuchsia::ui::scenic::Command>& PresentedCommands() { return presented_cmds_; }

 private:
  fidl::Binding<fuchsia::ui::scenic::Session> binding_;
  fuchsia::ui::scenic::SessionListenerPtr listener_;
  std::vector<fuchsia::ui::scenic::Command> cmd_queue_;
  std::vector<fuchsia::ui::scenic::Command> presented_cmds_;
};

class FakeScenic : public fuchsia::ui::scenic::testing::Scenic_TestBase {
 public:
  explicit FakeScenic(MockSession* mock_session) : mock_session_(mock_session) {}

  void NotImplemented_(const std::string& name) override {}

  void CreateSession(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) override {
    mock_session_->Bind(std::move(session), listener.Bind());
    create_session_called_ = true;
  }

  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  bool create_session_called() { return create_session_called_; }

 private:
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
  MockSession* mock_session_;
  bool create_session_called_;
};

class AnnotationViewTest : public gtest::TestLoopFixture {
 public:
  AnnotationViewTest() = default;

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    mock_session_ = std::make_unique<MockSession>();
    fake_scenic_ = std::make_unique<FakeScenic>(mock_session_.get());
    mock_annotation_registry_ = std::make_unique<MockAnnotationRegistry>();
    view_manager_ =
        std::make_unique<ViewManager>(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                                      context_provider_.context()->outgoing()->debug_dir());
    mock_semantic_provider_ =
        std::make_unique<accessibility_test::MockSemanticProvider>(view_manager_.get());

    context_provider_.service_directory_provider()->AddService(fake_scenic_->GetHandler());
    context_provider_.service_directory_provider()->AddService(
        mock_annotation_registry_->GetHandler());

    RunLoopUntilIdle();
  }

  fuchsia::ui::views::ViewRef CreateOrphanViewRef() {
    fuchsia::ui::views::ViewRef view_ref;

    zx::eventpair::create(0u, &view_ref.reference, &eventpair_peer_);
    return view_ref;
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSession> mock_session_;
  std::unique_ptr<FakeScenic> fake_scenic_;
  std::unique_ptr<MockAnnotationRegistry> mock_annotation_registry_;
  std::unique_ptr<ViewManager> view_manager_;
  std::unique_ptr<accessibility_test::MockSemanticProvider> mock_semantic_provider_;
  zx::eventpair eventpair_peer_;
};

TEST_F(AnnotationViewTest, TestViewCreateAndInit) {
  auto view_ref = CreateOrphanViewRef();
  AnnotationView annotation_view(context_provider_.context(), view_manager_.get(),
                                 GetKoid(view_ref));
  annotation_view.InitializeView(std::move(view_ref));

  RunLoopUntilIdle();

  EXPECT_TRUE(mock_annotation_registry_->create_annotation_view_holder_called());
}

}  // namespace
}  // namespace a11y
