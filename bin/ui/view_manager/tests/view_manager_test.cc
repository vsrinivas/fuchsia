// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include "lib/ui/tests/mocks/mock_view_container_listener.h"
#include "lib/ui/tests/mocks/mock_view_listener.h"
#include "lib/ui/tests/mocks/mock_view_tree_listener.h"

extern std::unique_ptr<component::StartupContext> g_startup_context;

namespace view_manager {
namespace test {

class ViewManagerTest : public ::testing::Test {
 protected:
  static void SetUpTestCase() {
    view_manager_ = g_startup_context->ConnectToEnvironmentService<
        ::fuchsia::ui::viewsv1::ViewManager>();
  }

  static ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager_;
};

::fuchsia::ui::viewsv1::ViewManagerPtr ViewManagerTest::view_manager_;

TEST_F(ViewManagerTest, CreateAViewManager) {
  ASSERT_TRUE(view_manager_.is_bound());
}

TEST_F(ViewManagerTest, CreateAView) {
  ASSERT_TRUE(view_manager_.is_bound());

  // Create and bind a mock view listener
  ::fuchsia::ui::viewsv1::ViewListenerPtr view_listener;
  mozart::test::MockViewListener mock_view_listener;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewListener> view_listener_binding(
      &mock_view_listener, view_listener.NewRequest());

  // Create a view
  ::fuchsia::ui::viewsv1::ViewPtr view;
  ::fuchsia::ui::viewsv1token::ViewOwnerPtr view_owner;
  view_manager_->CreateView(view.NewRequest(), view_owner.NewRequest(),
                            std::move(view_listener), "test_view");

  // Call View::GetToken. Check that you get the callback.
  int view_token_callback_invokecount = 0;
  auto view_token_callback =
      [&view_token_callback_invokecount](
          ::fuchsia::ui::viewsv1token::ViewTokenPtr token) {
        view_token_callback_invokecount++;
      };

  EXPECT_EQ(0, view_token_callback_invokecount);
  view->GetToken(view_token_callback);

  RUN_MESSAGE_LOOP_UNTIL(view_token_callback_invokecount == 1);

  EXPECT_EQ(1, view_token_callback_invokecount);
}

TEST_F(ViewManagerTest, CreateAChildView) {
  // Create and bind a mock view listener for a parent view
  ::fuchsia::ui::viewsv1::ViewListenerPtr parent_view_listener;
  mozart::test::MockViewListener parent_mock_view_listener;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewListener>
      child_view_listener_binding(&parent_mock_view_listener,
                                  parent_view_listener.NewRequest());

  // Create a parent view
  ::fuchsia::ui::viewsv1::ViewPtr parent_view;
  ::fuchsia::ui::viewsv1token::ViewOwnerPtr parent_view_owner;
  view_manager_->CreateView(
      parent_view.NewRequest(), parent_view_owner.NewRequest(),
      std::move(parent_view_listener), "parent_test_view");

  ::fuchsia::ui::viewsv1::ViewContainerPtr parent_view_container;
  parent_view->GetContainer(parent_view_container.NewRequest());

  // Create and bind a mock view listener for a child view
  ::fuchsia::ui::viewsv1::ViewListenerPtr child_view_listener;
  mozart::test::MockViewListener child_mock_view_listener;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewListener>
      parent_view_listener_binding(&child_mock_view_listener,
                                   child_view_listener.NewRequest());

  // Create a child view
  ::fuchsia::ui::viewsv1::ViewPtr child_view;
  ::fuchsia::ui::viewsv1token::ViewOwnerPtr child_view_owner;
  view_manager_->CreateView(child_view.NewRequest(),
                            child_view_owner.NewRequest(),
                            std::move(child_view_listener), "test_view");

  // Add the view to the parent
  parent_view_container->AddChild(0, std::move(child_view_owner));

  // Remove the view from the parent
  ::fuchsia::ui::viewsv1token::ViewOwnerPtr new_child_view_owner;
  parent_view_container->RemoveChild(0, new_child_view_owner.NewRequest());

  // If we had a ViewContainerListener, we would still not get a OnViewAttached
  // since the view hasn't had enough time to be resolved

  // Call View::GetToken. Check that you get the callback.
  int view_token_callback_invokecount = 0;
  auto view_token_callback =
      [&view_token_callback_invokecount](
          ::fuchsia::ui::viewsv1token::ViewTokenPtr token) {
        view_token_callback_invokecount++;
      };

  EXPECT_EQ(0, view_token_callback_invokecount);
  child_view->GetToken(view_token_callback);

  RUN_MESSAGE_LOOP_UNTIL(view_token_callback_invokecount == 1);

  EXPECT_EQ(1, view_token_callback_invokecount);
}

TEST_F(ViewManagerTest, SetChildProperties) {
  int32_t parent_view_width = 800;
  int32_t parent_view_height = 600;
  uint32_t parent_key = 0;
  uint32_t parent_scene_version = 1;
  uint32_t invalidation_count = 0;
  int32_t child_view_width = 800;
  int32_t child_view_height = 600;
  uint32_t child_key = 0;
  uint32_t child_scene_version = 1;

  // Create tree
  ::fuchsia::ui::viewsv1::ViewTreePtr tree;
  ::fuchsia::ui::viewsv1::ViewTreeListenerPtr tree_listener;
  mozart::test::MockViewTreeListener mock_tree_view_listener;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewTreeListener>
      tree_listener_binding(&mock_tree_view_listener,
                            tree_listener.NewRequest());
  view_manager_->CreateViewTree(tree.NewRequest(), std::move(tree_listener),
                                "test_view_tree");

  // Get tree's container and wire up listener
  ::fuchsia::ui::viewsv1::ViewContainerPtr tree_container;
  tree->GetContainer(tree_container.NewRequest());
  ::fuchsia::ui::viewsv1::ViewContainerListenerPtr tree_container_listener;
  mozart::test::MockViewContainerListener mock_tree_container_listener;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewContainerListener>
      tree_container_listener_binding(&mock_tree_container_listener,
                                      tree_container_listener.NewRequest());
  tree_container->SetListener(std::move(tree_container_listener));

  // Create and bind a mock view listener for a parent view
  ::fuchsia::ui::viewsv1::ViewListenerPtr parent_view_listener;
  mozart::test::MockViewListener parent_mock_view_listener;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewListener>
      child_view_listener_binding(&parent_mock_view_listener,
                                  parent_view_listener.NewRequest());

  // Create a parent view
  ::fuchsia::ui::viewsv1::ViewPtr parent_view;
  ::fuchsia::ui::viewsv1token::ViewOwnerPtr parent_view_owner;
  view_manager_->CreateView(
      parent_view.NewRequest(), parent_view_owner.NewRequest(),
      std::move(parent_view_listener), "parent_test_view");

  // Add root view to tree
  tree_container->AddChild(parent_key, std::move(parent_view_owner));

  auto parent_view_properties = ::fuchsia::ui::viewsv1::ViewProperties::New();
  parent_view_properties->view_layout =
      ::fuchsia::ui::viewsv1::ViewLayout::New();
  parent_view_properties->view_layout->size = fuchsia::math::Size::New();
  parent_view_properties->view_layout->size->width = parent_view_width;
  parent_view_properties->view_layout->size->height = parent_view_height;
  parent_view_properties->view_layout->inset = mozart::Inset::New();
  tree_container->SetChildProperties(parent_key, parent_scene_version,
                                     std::move(parent_view_properties));

  ::fuchsia::ui::viewsv1::ViewContainerPtr parent_view_container;
  parent_view->GetContainer(parent_view_container.NewRequest());

  // Create and bind a mock view listener for a child view
  ::fuchsia::ui::viewsv1::ViewListenerPtr child_view_listener;
  mozart::test::MockViewListener child_mock_view_listener(
      [&invalidation_count, child_view_width,
       child_view_height](mozart::ViewInvalidationPtr invalidation) {
        EXPECT_TRUE(invalidation->properties);
        EXPECT_EQ(child_view_width,
                  invalidation->properties->view_layout->size->width);
        EXPECT_EQ(child_view_height,
                  invalidation->properties->view_layout->size->height);
        invalidation_count++;
      });
  fidl::Binding<::fuchsia::ui::viewsv1::ViewListener>
      parent_view_listener_binding(&child_mock_view_listener,
                                   child_view_listener.NewRequest());

  // Create a child view
  ::fuchsia::ui::viewsv1::ViewPtr child_view;
  ::fuchsia::ui::viewsv1token::ViewOwnerPtr child_view_owner;
  view_manager_->CreateView(child_view.NewRequest(),
                            child_view_owner.NewRequest(),
                            std::move(child_view_listener), "test_view");

  // Add the view to the parent
  parent_view_container->AddChild(child_key, std::move(child_view_owner));

  auto view_properties = ::fuchsia::ui::viewsv1::ViewProperties::New();
  view_properties->view_layout = ::fuchsia::ui::viewsv1::ViewLayout::New();
  view_properties->view_layout->size = fuchsia::math::Size::New();
  view_properties->view_layout->size->width = child_view_width;
  view_properties->view_layout->size->height = child_view_height;
  view_properties->view_layout->inset = mozart::Inset::New();

  parent_view_container->SetChildProperties(child_key, child_scene_version,
                                            std::move(view_properties));

  RUN_MESSAGE_LOOP_WHILE(invalidation_count == 0);
  EXPECT_EQ(1u, invalidation_count);

  // If we had a ViewContainerListener, we would still not get a OnViewAttached
  // since the view hasn't had enough time to be resolved
}

}  // namespace test
}  // namespace view_manager
