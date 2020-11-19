// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/element_manager_impl.h"

#include <fuchsia/element/cpp/fidl.h>
#include <lib/fidl/cpp/optional.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"
#include "src/modular/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

using ::modular::annotations::AnnotationEq;
using ::testing::ByRef;
using ::testing::ElementsAre;

class ElementManagerImplTest : public modular_testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage();
    element_manager_impl_ = std::make_unique<ElementManagerImpl>(session_storage_.get());
    element_manager_impl_->Connect(element_manager_.NewRequest());
  }

  SessionStorage* session_storage() { return session_storage_.get(); }
  fuchsia::element::ManagerPtr& element_manager() { return element_manager_; }

 private:
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<ElementManagerImpl> element_manager_impl_;
  fuchsia::element::ManagerPtr element_manager_;
};

// Tests that ProposeElement returns ProposeElementError::NOT_FOUND if ElementSpec does not
// have component_url set.
TEST_F(ElementManagerImplTest, ProposeElementMissingUrl) {
  fuchsia::element::Spec element_spec;

  bool is_proposed = false;
  element_manager()->ProposeElement(std::move(element_spec), /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_TRUE(result.is_err());
                                      EXPECT_EQ(fuchsia::element::ProposeElementError::NOT_FOUND,
                                                result.err());
                                      is_proposed = true;
                                    });

  RunLoopUntil([&]() { return is_proposed; });
}

// Tests that ProposeElement returns ProposeElementError::INVALID_ARGS if ElementSpec specifies
// |additional_services| without a valid |host_directory| channel.
TEST_F(ElementManagerImplTest, ProposeElementAdditionalServicesMissingHostDirectory) {
  static constexpr auto kElementComponentUrl =
      "fuchsia-pkg://fuchsia.com/test_element#meta/test_element.cmx";

  fuchsia::sys::ServiceList service_list;

  fuchsia::element::Spec element_spec;
  element_spec.set_component_url(kElementComponentUrl);
  element_spec.set_additional_services(std::move(service_list));

  bool is_proposed = false;
  element_manager()->ProposeElement(std::move(element_spec), /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_TRUE(result.is_err());
                                      EXPECT_EQ(fuchsia::element::ProposeElementError::INVALID_ARGS,
                                                result.err());
                                      is_proposed = true;
                                    });

  RunLoopUntil([&]() { return is_proposed; });
}

// Tests that ProposeElement returns ProposeElementError::INVALID_ARGS if ElementSpec specifies
// |additional_services| with a |provider|.
TEST_F(ElementManagerImplTest, ProposeElementAdditionalServicesWithProvider) {
  static constexpr auto kElementComponentUrl =
      "fuchsia-pkg://fuchsia.com/test_element#meta/test_element.cmx";

  fuchsia::sys::ServiceProviderHandle service_provider;

  // Bind |service_provider| to a valid channel.
  auto service_provider_request = service_provider.NewRequest();

  fuchsia::sys::ServiceList service_list;
  service_list.provider = std::move(service_provider);

  fuchsia::element::Spec element_spec;
  element_spec.set_component_url(kElementComponentUrl);
  element_spec.set_additional_services(std::move(service_list));

  bool is_proposed = false;
  element_manager()->ProposeElement(std::move(element_spec), /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_TRUE(result.is_err());
                                      EXPECT_EQ(fuchsia::element::ProposeElementError::INVALID_ARGS,
                                                result.err());
                                      is_proposed = true;
                                    });

  RunLoopUntil([&]() { return is_proposed; });
}

// Tests that ProposeElement creates a story with a single mod.
TEST_F(ElementManagerImplTest, ProposeElementCreatesStoryAndMod) {
  static constexpr auto kElementComponentUrl =
      "fuchsia-pkg://fuchsia.com/test_element#meta/test_element.cmx";

  fuchsia::element::Spec element_spec;
  element_spec.set_component_url(kElementComponentUrl);

  // No stories should exist.
  auto all_story_data = session_storage()->GetAllStoryData();
  ASSERT_TRUE(all_story_data.empty());

  bool is_proposed = false;
  element_manager()->ProposeElement(std::move(element_spec), /*controller=*/nullptr,
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });

  RunLoopUntil([&]() { return is_proposed; });

  // Proposing the element should create a new story.
  all_story_data = session_storage()->GetAllStoryData();
  ASSERT_EQ(1u, all_story_data.size());

  auto story_name = all_story_data.at(0).story_name();
  auto story_storage = session_storage()->GetStoryStorage(story_name);

  // The story should have a single mod.
  auto all_module_data = story_storage->ReadAllModuleData();
  ASSERT_EQ(1u, all_module_data.size());

  const auto& module_data = all_module_data.at(0);

  EXPECT_FALSE(module_data.module_deleted());
  EXPECT_EQ(kElementComponentUrl, module_data.module_url());
  EXPECT_EQ(kElementComponentUrl, module_data.intent().handler);
}

// Tests that ProposeElement binds the client's request for an ElementController.
TEST_F(ElementManagerImplTest, ProposeElementBindsElementController) {
  static constexpr auto kElementComponentUrl =
      "fuchsia-pkg://fuchsia.com/test_element#meta/test_element.cmx";

  fuchsia::element::ControllerPtr element_controller_ptr;

  fuchsia::element::Spec element_spec;
  element_spec.set_component_url(kElementComponentUrl);

  bool is_proposed = false;
  element_manager()->ProposeElement(std::move(element_spec), element_controller_ptr.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });

  RunLoopUntil([&]() { return is_proposed; });

  EXPECT_TRUE(element_controller_ptr.is_bound());
}

// Tests that closing an ElementController removes the element, deleting its story.
TEST_F(ElementManagerImplTest, ClosingElementControllerRemovesElement) {
  static constexpr auto kElementComponentUrl =
      "fuchsia-pkg://fuchsia.com/test_element#meta/test_element.cmx";

  fuchsia::element::ControllerPtr element_controller_ptr;

  fuchsia::element::Spec element_spec;
  element_spec.set_component_url(kElementComponentUrl);

  // No stories should exist.
  auto all_story_data = session_storage()->GetAllStoryData();
  ASSERT_TRUE(all_story_data.empty());

  bool is_proposed = false;
  element_manager()->ProposeElement(std::move(element_spec), element_controller_ptr.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });

  RunLoopUntil([&]() { return is_proposed; });

  // Proposing the element should create a new story.
  all_story_data = session_storage()->GetAllStoryData();
  ASSERT_EQ(1u, all_story_data.size());

  EXPECT_TRUE(element_controller_ptr.is_bound());

  element_controller_ptr.Unbind();
  RunLoopUntilIdle();

  EXPECT_FALSE(element_controller_ptr.is_bound());

  // The story should be deleted.
  all_story_data = session_storage()->GetAllStoryData();
  EXPECT_TRUE(all_story_data.empty());
}

// Tests that ProposeElement creates a story with annotations from the element spec.
TEST_F(ElementManagerImplTest, ProposeElementAnnotatesStory) {
  static constexpr auto kElementComponentUrl =
      "fuchsia-pkg://fuchsia.com/test_element#meta/test_element.cmx";
  static constexpr auto kTestAnnotationKey = "test_annotation_key";
  static constexpr auto kTestAnnotationValue = "test_annotation_value";

  fuchsia::element::ControllerPtr element_controller_ptr;

  // The element spec has a single initial annotation.
  std::vector<fuchsia::element::Annotation> element_annotations;
  auto element_annotation_key = fuchsia::element::AnnotationKey{
      .namespace_ = element::annotations::kGlobalNamespace, .value = kTestAnnotationKey};
  auto element_annotation = fuchsia::element::Annotation{
      .key = fidl::Clone(element_annotation_key),
      .value = fuchsia::element::AnnotationValue::WithText(kTestAnnotationValue)};
  element_annotations.push_back(std::move(element_annotation));

  fuchsia::element::Spec element_spec;
  element_spec.set_component_url(kElementComponentUrl);
  element_spec.set_annotations(std::move(element_annotations));

  // No stories should exist.
  auto all_story_data = session_storage()->GetAllStoryData();
  ASSERT_TRUE(all_story_data.empty());

  bool is_proposed = false;
  element_manager()->ProposeElement(std::move(element_spec), element_controller_ptr.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_proposed = true;
                                    });

  RunLoopUntil([&]() { return is_proposed; });

  // Proposing the element should create a new story.
  all_story_data = session_storage()->GetAllStoryData();
  ASSERT_EQ(1u, all_story_data.size());

  auto& story_data = all_story_data.at(0);
  ASSERT_TRUE(story_data.has_story_info());

  // The story should have an equivalent Modular annotation.
  auto modular_annotation = fuchsia::modular::Annotation{
      .key = element::annotations::ToModularAnnotationKey(element_annotation_key),
      .value =
          fidl::MakeOptional(fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))};

  EXPECT_TRUE(story_data.story_info().has_annotations());
  EXPECT_THAT(story_data.story_info().annotations(),
              ElementsAre(AnnotationEq(ByRef(modular_annotation))));
}

// Tests that ProposeElement creates separate stories for elements and that deleting one
// does not affect the other.
TEST_F(ElementManagerImplTest, ProposeElementCreatesSeparateStories) {
  static constexpr auto kElementComponentUrl =
      "fuchsia-pkg://fuchsia.com/test_element#meta/test_element.cmx";

  fuchsia::element::ControllerPtr first_element_controller_ptr;
  fuchsia::element::Spec first_element_spec;
  first_element_spec.set_component_url(kElementComponentUrl);

  fuchsia::element::ControllerPtr second_element_controller_ptr;
  fuchsia::element::Spec second_element_spec;
  second_element_spec.set_component_url(kElementComponentUrl);

  // No stories should exist.
  auto all_story_data = session_storage()->GetAllStoryData();
  ASSERT_TRUE(all_story_data.empty());

  // Propose the first element.
  bool is_first_proposed = false;
  element_manager()->ProposeElement(std::move(first_element_spec),
                                    first_element_controller_ptr.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_first_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_first_proposed; });

  EXPECT_TRUE(first_element_controller_ptr.is_bound());

  // Proposing the first element should create a new story.
  all_story_data = session_storage()->GetAllStoryData();
  ASSERT_EQ(1u, all_story_data.size());
  auto first_story_id = all_story_data.at(0).story_info().id();

  // Propose the second element.
  bool is_second_proposed = false;
  element_manager()->ProposeElement(std::move(second_element_spec),
                                    second_element_controller_ptr.NewRequest(),
                                    [&](fuchsia::element::Manager_ProposeElement_Result result) {
                                      EXPECT_FALSE(result.is_err());
                                      is_second_proposed = true;
                                    });
  RunLoopUntil([&]() { return is_second_proposed; });

  EXPECT_TRUE(second_element_controller_ptr.is_bound());

  // Proposing the second element should create a new story.
  all_story_data = session_storage()->GetAllStoryData();
  ASSERT_EQ(2u, all_story_data.size());

  // Delete the first story.
  bool is_first_story_deleted = false;
  session_storage()->SubscribeStoryDeleted([&](const std::string& story_id) {
    EXPECT_EQ(first_story_id, story_id);
    is_first_story_deleted = true;
    return WatchInterest::kStop;
  });

  session_storage()->DeleteStory(first_story_id);

  RunLoopUntil([&]() { return is_first_story_deleted; });

  // Deleting the first story should close the first element's ElementController.
  RunLoopUntil([&]() { return !first_element_controller_ptr.is_bound(); });

  // The second story and element should remain.
  all_story_data = session_storage()->GetAllStoryData();
  ASSERT_EQ(1u, all_story_data.size());

  EXPECT_TRUE(second_element_controller_ptr.is_bound());
}

}  // namespace
}  // namespace modular
