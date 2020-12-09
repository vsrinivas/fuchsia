// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/element/cpp/fidl.h>
#include <fuchsia/testing/modular/cpp/fidl.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_session_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_story_shell.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"

namespace {

using ::testing::ByRef;
using ::testing::ElementsAre;

class FakeElement : public modular_testing::FakeComponent {
 public:
  explicit FakeElement(modular_testing::FakeComponent::Args args) : FakeComponent(std::move(args)) {
    spec_.set_component_url(url());
  };

  ~FakeElement() override = default;

  // Instantiates a FakeElement with a randomly generated URL and default sandbox services
  // (see GetDefaultSandboxServices()).
  static std::unique_ptr<FakeElement> CreateWithDefaultOptions() {
    return std::make_unique<FakeElement>(modular_testing::FakeComponent::Args{
        .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
        .sandbox_services = GetDefaultSandboxServices()});
  }

  // Returns the default list of services (capabilities) an element expects in its namespace.
  //
  // Default services:
  //  * fuchsia.testing.modular.TestProtocol
  static std::vector<std::string> GetDefaultSandboxServices() {
    return {fuchsia::testing::modular::TestProtocol::Name_};
  }

  // Returns a Spec that can be used to propose this element.
  const fuchsia::element::Spec& spec() const { return spec_; }

  // Sets a function to be called when the element's component is created.
  void set_on_create(fit::function<void(fuchsia::sys::StartupInfo)> on_create) {
    on_create_ = std::move(on_create);
  }

  // Sets a function to be called when the element's component is destroyed.
  void set_on_destroy(fit::function<void()> on_destroy) { on_destroy_ = std::move(on_destroy); }

 protected:
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override {
    on_create_(std::move(startup_info));
  }

  void OnDestroy() override { on_destroy_(); }

 private:
  fuchsia::element::Spec spec_;
  fit::function<void(fuchsia::sys::StartupInfo)> on_create_ =
      [](fuchsia::sys::StartupInfo /*unused*/) {};
  fit::function<void()> on_destroy_ = []() {};
};

class ElementManagerTest : public modular_testing::TestHarnessFixture {
 protected:
  ElementManagerTest()
      : session_shell_(modular_testing::FakeSessionShell::CreateWithDefaultOptions()),
        story_shell_(modular_testing::FakeStoryShell::CreateWithDefaultOptions()),
        element_(FakeElement::CreateWithDefaultOptions()) {}

  fuchsia::modular::PuppetMasterPtr& puppet_master() { return puppet_master_; }
  fuchsia::element::ManagerPtr& element_manager() { return element_manager_; }
  modular_testing::FakeSessionShell* session_shell() { return session_shell_.get(); }
  modular_testing::FakeStoryShell* story_shell() { return story_shell_.get(); }
  FakeElement* element() { return element_.get(); }

  void StartSession() {
    modular_testing::TestHarnessBuilder builder;
    builder.InterceptSessionShell(session_shell_->BuildInterceptOptions());
    builder.InterceptStoryShell(story_shell_->BuildInterceptOptions());
    builder.InterceptComponent(element_->BuildInterceptOptions());
    builder.BuildAndRun(test_harness());

    fuchsia::modular::testing::ModularService request;
    request.set_puppet_master(puppet_master_.NewRequest());
    request.set_element_manager(element_manager_.NewRequest());
    test_harness()->ConnectToModularService(std::move(request));

    // Wait for the session shell to start.
    RunLoopUntil([this] { return session_shell_->is_running(); });
  }

 private:
  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::element::ManagerPtr element_manager_;
  std::unique_ptr<modular_testing::FakeSessionShell> session_shell_;
  std::unique_ptr<modular_testing::FakeStoryShell> story_shell_;
  std::unique_ptr<FakeElement> element_;
};

// Tests that ElementManager.ProposeElement creates the element's component.
TEST_F(ElementManagerTest, ProposeCreatesElement) {
  bool is_element_created{false};
  element()->set_on_create(
      [&is_element_created](fuchsia::sys::StartupInfo /*unused*/) { is_element_created = true; });

  StartSession();

  // Propose the element.
  bool is_proposed{false};
  element_manager()->ProposeElement(fidl::Clone(element()->spec()),
                                    /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed; });

  RunLoopUntil([&]() { return is_element_created; });
  EXPECT_TRUE(is_element_created);
}

// Tests that ElementManager.ProposeElement starts a story.
TEST_F(ElementManagerTest, ProposeStartsStory) {
  StartSession();

  fuchsia::modular::StoryProvider* story_provider = session_shell()->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  // Proposing the element should create and start a story.
  bool has_story_started{false};
  modular_testing::SimpleStoryProviderWatcher watcher;
  watcher.set_on_change_2([&has_story_started](fuchsia::modular::StoryInfo2 /*unused*/,
                                               fuchsia::modular::StoryState story_state,
                                               fuchsia::modular::StoryVisibilityState /*unused*/) {
    if (story_state == fuchsia::modular::StoryState::RUNNING) {
      has_story_started = true;
    }
  });

  bool is_watcher_added{false};
  fit::function<void(std::vector<fuchsia::modular::StoryInfo2>)> on_get_stories =
      [&](const std::vector<fuchsia::modular::StoryInfo2>& story_infos) {
        ASSERT_TRUE(story_infos.empty());
        is_watcher_added = true;
      };
  watcher.Watch(story_provider, &on_get_stories);
  RunLoopUntil([&]() { return is_watcher_added; });

  // Propose the element.
  bool is_proposed{false};
  element_manager()->ProposeElement(fidl::Clone(element()->spec()),
                                    /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed; });

  // The story should have started.
  RunLoopUntil([&]() { return has_story_started; });
  EXPECT_TRUE(has_story_started);
}

// Tests that closing the element Controller deletes the element story.
TEST_F(ElementManagerTest, ClosingElementControllerDeletesStory) {
  StartSession();

  fuchsia::modular::StoryProvider* story_provider = session_shell()->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  // Proposing the element should create and start a story.
  bool has_story_started{false};
  bool has_story_stopped{false};
  modular_testing::SimpleStoryProviderWatcher watcher;
  watcher.set_on_change_2(
      [&has_story_started, &has_story_stopped](fuchsia::modular::StoryInfo2 /*unused*/,
                                               fuchsia::modular::StoryState story_state,
                                               fuchsia::modular::StoryVisibilityState /*unused*/) {
        if (story_state == fuchsia::modular::StoryState::RUNNING) {
          has_story_started = true;
        } else if (has_story_started && story_state == fuchsia::modular::StoryState::STOPPED) {
          has_story_stopped = true;
        }
      });

  bool is_watcher_added{false};
  fit::function<void(std::vector<fuchsia::modular::StoryInfo2>)> on_get_stories =
      [&](const std::vector<fuchsia::modular::StoryInfo2>& story_infos) {
        ASSERT_TRUE(story_infos.empty());
        is_watcher_added = true;
      };
  watcher.Watch(story_provider, &on_get_stories);
  RunLoopUntil([&]() { return is_watcher_added; });

  fuchsia::element::ControllerPtr element_controller;

  // Propose the element.
  bool is_proposed{false};
  element_manager()->ProposeElement(fidl::Clone(element()->spec()), element_controller.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed; });

  // The story should have started.
  RunLoopUntil([&]() { return has_story_started; });
  EXPECT_TRUE(has_story_started);

  // Close the ElementController.
  element_controller.Unbind().TakeChannel().reset();

  // The story should have stopped.
  RunLoopUntil([&]() { return has_story_stopped; });
  EXPECT_TRUE(has_story_stopped);

  // The story should have been deleted.
  bool got_stories{false};
  story_provider->GetStories2(
      /*watcher=*/nullptr, [&](std::vector<fuchsia::modular::StoryInfo2> story_infos) {
        EXPECT_TRUE(story_infos.empty());
        got_stories = true;
      });
  RunLoopUntil([&] { return got_stories; });
}

// Tests that ElementManager.ProposeElement adds the element's view as a surface in the story shell.
TEST_F(ElementManagerTest, ProposeAddsSurfaceToStoryShell) {
  StartSession();

  // The element module's surface will be added to the story shell.
  bool is_surface_added{false};
  story_shell()->set_on_add_surface(
      [&is_surface_added](fuchsia::modular::ViewConnection /*unused*/,
                          fuchsia::modular::SurfaceInfo2 /*unused*/) { is_surface_added = true; });

  // Propose the element.
  bool is_proposed{false};
  element_manager()->ProposeElement(fidl::Clone(element()->spec()),
                                    /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed; });

  // The story shell should receive the element's view.
  RunLoopUntil([&]() { return is_surface_added; });
}

// Tests that ElementManager.ProposeElement creates a story containing the annotations from
// the Spec.
TEST_F(ElementManagerTest, ProposeAnnotatesStory) {
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  StartSession();

  fuchsia::modular::StoryProvider* story_provider = session_shell()->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  // Create a Spec with an annotation.
  auto element_annotation = fuchsia::element::Annotation{
      .key = fuchsia::element::AnnotationKey{.namespace_ = "global", .value = kTestAnnotationKey},
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};

  auto element_spec = fidl::Clone(element()->spec());
  element_spec.mutable_annotations()->push_back(std::move(element_annotation));

  // Propose the element.
  bool is_proposed{false};
  element_manager()->ProposeElement(std::move(element_spec), /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed; });

  // The story should have the annotation.
  bool got_annotations{false};
  story_provider->GetStories2(
      /*watcher=*/nullptr, [&](std::vector<fuchsia::modular::StoryInfo2> story_infos) {
        ASSERT_EQ(1u, story_infos.size());
        auto& story_info = story_infos.at(0);

        auto modular_annotation = fuchsia::modular::Annotation{
            .key = kTestAnnotationKey,
            .value = fidl::MakeOptional(
                fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))};

        ASSERT_TRUE(story_info.has_annotations());
        EXPECT_THAT(story_info.annotations(),
                    ElementsAre(modular::annotations::AnnotationEq(ByRef(modular_annotation))));

        got_annotations = true;
      });
  RunLoopUntil([&] { return got_annotations; });
}

// Tests that ElementController.GetAnnotations returns the annotations initially proposed
// on the element.
TEST_F(ElementManagerTest, ElementControllerGetAnnotations) {
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  StartSession();

  fuchsia::modular::StoryProvider* story_provider = session_shell()->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  fuchsia::element::ControllerPtr element_controller;

  // Create an ElementSpec with an annotation.
  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};

  auto element_spec = fidl::Clone(element()->spec());
  element_spec.mutable_annotations()->push_back(fidl::Clone(element_annotation));

  // Propose the element.
  bool is_proposed{false};
  element_manager()->ProposeElement(std::move(element_spec), element_controller.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed; });

  // The story should have the annotation.
  bool got_annotations{false};
  element_controller->GetAnnotations(
      [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
        ASSERT_FALSE(result.is_err());

        EXPECT_THAT(result.response().annotations,
                    ElementsAre(element::annotations::AnnotationEq(ByRef(element_annotation))));

        got_annotations = true;
      });
  RunLoopUntil([&] { return got_annotations; });
}

// Tests that ElementController.UpdateAnnotations sets annotations on the element story.
TEST_F(ElementManagerTest, ElementControllerSetAnnotations) {
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  StartSession();

  fuchsia::modular::StoryProvider* story_provider = session_shell()->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  fuchsia::element::ControllerPtr element_controller;

  // Propose the element.
  bool is_proposed{false};
  element_manager()->ProposeElement(fidl::Clone(element()->spec()), element_controller.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed; });

  // The story should initially have an empty list of annotations.
  bool got_story_annotations_before{false};
  story_provider->GetStories2(/*watcher=*/nullptr,
                              [&](std::vector<fuchsia::modular::StoryInfo2> story_infos) {
                                ASSERT_EQ(1u, story_infos.size());
                                auto& story_info = story_infos.at(0);

                                ASSERT_TRUE(story_info.has_annotations());
                                EXPECT_TRUE(story_info.annotations().empty());

                                got_story_annotations_before = true;
                              });
  RunLoopUntil([&] { return got_story_annotations_before; });

  // Set the element's annotations.
  auto element_annotation = fuchsia::element::Annotation{
      .key = modular::annotations::ToElementAnnotationKey(kTestAnnotationKey),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};

  std::vector<fuchsia::element::Annotation> annotations_to_set;
  annotations_to_set.push_back(fidl::Clone(element_annotation));

  bool did_update_annotations{false};
  element_controller->UpdateAnnotations(
      std::move(annotations_to_set),
      /*annotations_to_delete=*/{},
      [&](fuchsia::element::AnnotationController_UpdateAnnotations_Result result) {
        EXPECT_FALSE(result.is_err());
        did_update_annotations = true;
      });
  RunLoopUntil([&] { return did_update_annotations; });

  // The story should have the new annotation.
  bool got_story_annotations_after{false};
  story_provider->GetStories2(
      /*watcher=*/nullptr, [&](std::vector<fuchsia::modular::StoryInfo2> story_infos) {
        ASSERT_EQ(1u, story_infos.size());
        auto& story_info = story_infos.at(0);

        auto modular_annotation = fuchsia::modular::Annotation{
            .key = kTestAnnotationKey,
            .value = fidl::MakeOptional(
                fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))};

        ASSERT_TRUE(story_info.has_annotations());
        EXPECT_THAT(story_info.annotations(),
                    ElementsAre(modular::annotations::AnnotationEq(ByRef(modular_annotation))));

        got_story_annotations_after = true;
      });
  RunLoopUntil([&] { return got_story_annotations_after; });

  // The element should have the annotation.
  bool got_element_annotations{false};
  element_controller->GetAnnotations(
      [&](fuchsia::element::AnnotationController_GetAnnotations_Result result) {
        ASSERT_FALSE(result.is_err());

        EXPECT_THAT(result.response().annotations,
                    ElementsAre(element::annotations::AnnotationEq(ByRef(element_annotation))));

        got_element_annotations = true;
      });
  RunLoopUntil([&] { return got_element_annotations; });
}

// Tests that ElementManager.ProposeElement with an ElementSpec that contains
// |additional_services| offers them to the launched element.
TEST_F(ElementManagerTest, ProposeOffersServices) {
  StartSession();

  fuchsia::modular::StoryProvider* story_provider = session_shell()->story_provider();
  ASSERT_TRUE(story_provider != nullptr);

  // Build a directory to serve the ServiceList passed to the element.
  int connect_count = 0;
  auto dir = std::make_unique<vfs::PseudoDir>();
  dir->AddEntry(
      fuchsia::testing::modular::TestProtocol::Name_,
      std::make_unique<vfs::Service>(
          [&](zx::channel /*unused*/, async_dispatcher_t* /*unused*/) { ++connect_count; }));
  auto dir_server = std::make_unique<modular::PseudoDirServer>(std::move(dir));

  // Construct a ServiceList with the above dir server.
  fuchsia::sys::ServiceList service_list;
  service_list.names.push_back(fuchsia::testing::modular::TestProtocol::Name_);
  service_list.host_directory = dir_server->Serve().Unbind().TakeChannel();

  // Create an ElementSpec with the ServiceList in |additional_services|.
  auto element_spec = fidl::Clone(element()->spec());
  element_spec.set_additional_services(std::move(service_list));

  // Propose the element.
  bool is_proposed{false};
  element_manager()->ProposeElement(std::move(element_spec), /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_proposed; });

  // The element must be running to use its ComponentContext.
  RunLoopUntil([&]() { return element()->is_running(); });

  // Connect to the provided service from the element.
  auto test_ptr =
      element()->component_context()->svc()->Connect<fuchsia::testing::modular::TestProtocol>();
  RunLoopUntil([&] { return connect_count > 0; });
  EXPECT_EQ(1, connect_count);
}

}  // namespace
