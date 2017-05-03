// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "apps/mozart/src/view_manager/tests/mocks/mock_renderer.h"
#include "apps/mozart/src/view_manager/tests/mocks/mock_view_associate.h"
#include "apps/mozart/src/view_manager/tests/mocks/mock_view_container_listener.h"
#include "apps/mozart/src/view_manager/tests/mocks/mock_view_listener.h"
#include "apps/mozart/src/view_manager/tests/mocks/mock_view_tree_listener.h"
#include "apps/mozart/src/view_manager/tests/test_with_message_loop.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/time/time_point.h"
#include "lib/ftl/time/time_delta.h"

extern mozart::ViewManagerPtr g_view_manager;

namespace view_manager {
namespace test {

class ViewManagerTest : public TestWithMessageLoop {};

TEST_F(ViewManagerTest, CreateAViewManager) {
  ASSERT_TRUE(g_view_manager.is_bound());
}

TEST_F(ViewManagerTest, CreateAView) {
  ASSERT_TRUE(g_view_manager.is_bound());

  // Create and bind a mock view listener
  mozart::ViewListenerPtr view_listener;
  MockViewListener mock_view_listener;
  fidl::Binding<mozart::ViewListener> view_listener_binding(
      &mock_view_listener, view_listener.NewRequest());

  // Create a view
  mozart::ViewPtr view;
  mozart::ViewOwnerPtr view_owner;
  g_view_manager->CreateView(view.NewRequest(), view_owner.NewRequest(),
                             std::move(view_listener), "test_view");

  // Call View::GetToken. Check that you get the callback.
  int view_token_callback_invokecount = 0;
  auto view_token_callback = [&view_token_callback_invokecount](
      mozart::ViewTokenPtr token) { view_token_callback_invokecount++; };

  EXPECT_EQ(0, view_token_callback_invokecount);
  view->GetToken(view_token_callback);

  RUN_MESSAGE_LOOP_WHILE(view_token_callback_invokecount != 1);

  EXPECT_EQ(1, view_token_callback_invokecount);
}

TEST_F(ViewManagerTest, CreateAChildView) {
  // Create and bind a mock view listener for a parent view
  mozart::ViewListenerPtr parent_view_listener;
  MockViewListener parent_mock_view_listener;
  fidl::Binding<mozart::ViewListener> child_view_listener_binding(
      &parent_mock_view_listener, parent_view_listener.NewRequest());

  // Create a parent view
  mozart::ViewPtr parent_view;
  mozart::ViewOwnerPtr parent_view_owner;
  g_view_manager->CreateView(
      parent_view.NewRequest(), parent_view_owner.NewRequest(),
      std::move(parent_view_listener), "parent_test_view");

  mozart::ViewContainerPtr parent_view_container;
  parent_view->GetContainer(parent_view_container.NewRequest());

  // Create and bind a mock view listener for a child view
  mozart::ViewListenerPtr child_view_listener;
  MockViewListener child_mock_view_listener;
  fidl::Binding<mozart::ViewListener> parent_view_listener_binding(
      &child_mock_view_listener, child_view_listener.NewRequest());

  // Create a child view
  mozart::ViewPtr child_view;
  mozart::ViewOwnerPtr child_view_owner;
  g_view_manager->CreateView(child_view.NewRequest(),
                             child_view_owner.NewRequest(),
                             std::move(child_view_listener), "test_view");

  // Add the view to the parent
  parent_view_container->AddChild(0, std::move(child_view_owner));

  // Remove the view from the parent
  mozart::ViewOwnerPtr new_child_view_owner;
  parent_view_container->RemoveChild(0, new_child_view_owner.NewRequest());

  // If we had a ViewContainerListener, we would still not get a OnViewAttached
  // since the view hasn't had enough time to be resolved

  // Call View::GetToken. Check that you get the callback.
  int view_token_callback_invokecount = 0;
  auto view_token_callback = [&view_token_callback_invokecount](
      mozart::ViewTokenPtr token) { view_token_callback_invokecount++; };

  EXPECT_EQ(0, view_token_callback_invokecount);
  child_view->GetToken(view_token_callback);

  RUN_MESSAGE_LOOP_WHILE(view_token_callback_invokecount != 1);

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
  mozart::ViewTreePtr tree;
  mozart::ViewTreeListenerPtr tree_listener;
  MockViewTreeListener mock_tree_view_listener;
  fidl::Binding<mozart::ViewTreeListener> tree_listener_binding(
      &mock_tree_view_listener, tree_listener.NewRequest());
  g_view_manager->CreateViewTree(tree.NewRequest(), std::move(tree_listener),
                                 "test_view_tree");

  // Get tree's container and wire up listener
  mozart::ViewContainerPtr tree_container;
  tree->GetContainer(tree_container.NewRequest());
  mozart::ViewContainerListenerPtr tree_container_listener;
  MockViewContainerListener mock_tree_container_listener;
  fidl::Binding<mozart::ViewContainerListener> tree_container_listener_binding(
      &mock_tree_container_listener, tree_container_listener.NewRequest());
  tree_container->SetListener(std::move(tree_container_listener));

  mozart::RendererPtr renderer;
  MockRenderer mock_renderer;
  fidl::Binding<mozart::Renderer> renderer_binding(&mock_renderer,
                                                   renderer.NewRequest());
  tree->SetRenderer(std::move(renderer));

  // Create and bind a mock view listener for a parent view
  mozart::ViewListenerPtr parent_view_listener;
  MockViewListener parent_mock_view_listener;
  fidl::Binding<mozart::ViewListener> child_view_listener_binding(
      &parent_mock_view_listener, parent_view_listener.NewRequest());

  // Create a parent view
  mozart::ViewPtr parent_view;
  mozart::ViewOwnerPtr parent_view_owner;
  g_view_manager->CreateView(
      parent_view.NewRequest(), parent_view_owner.NewRequest(),
      std::move(parent_view_listener), "parent_test_view");

  // Add root view to tree
  tree_container->AddChild(parent_key, std::move(parent_view_owner));

  auto parent_view_properties = mozart::ViewProperties::New();
  parent_view_properties->display_metrics = mozart::DisplayMetrics::New();
  parent_view_properties->display_metrics->device_pixel_ratio = 1.0;
  parent_view_properties->view_layout = mozart::ViewLayout::New();
  parent_view_properties->view_layout->size = mozart::Size::New();
  parent_view_properties->view_layout->size->width = parent_view_width;
  parent_view_properties->view_layout->size->height = parent_view_height;
  parent_view_properties->view_layout->inset = mozart::Inset::New();
  tree_container->SetChildProperties(parent_key, parent_scene_version,
                                     std::move(parent_view_properties));

  mozart::ViewContainerPtr parent_view_container;
  parent_view->GetContainer(parent_view_container.NewRequest());

  // Create and bind a mock view listener for a child view
  mozart::ViewListenerPtr child_view_listener;
  MockViewListener child_mock_view_listener(
      [&invalidation_count, child_view_width,
       child_view_height](mozart::ViewInvalidationPtr invalidation) {
        EXPECT_TRUE(invalidation->properties);
        EXPECT_EQ(child_view_width,
                  invalidation->properties->view_layout->size->width);
        EXPECT_EQ(child_view_height,
                  invalidation->properties->view_layout->size->height);
        invalidation_count++;
      });
  fidl::Binding<mozart::ViewListener> parent_view_listener_binding(
      &child_mock_view_listener, child_view_listener.NewRequest());

  // Create a child view
  mozart::ViewPtr child_view;
  mozart::ViewOwnerPtr child_view_owner;
  g_view_manager->CreateView(child_view.NewRequest(),
                             child_view_owner.NewRequest(),
                             std::move(child_view_listener), "test_view");

  // Add the view to the parent
  parent_view_container->AddChild(child_key, std::move(child_view_owner));

  auto view_properties = mozart::ViewProperties::New();
  view_properties->view_layout = mozart::ViewLayout::New();
  view_properties->display_metrics = mozart::DisplayMetrics::New();
  view_properties->display_metrics->device_pixel_ratio = 1.0;
  view_properties->view_layout->size = mozart::Size::New();
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

TEST_F(ViewManagerTest, ConnectAMockViewAssociate) {
  // Create and bind a MockViewAssociate
  fidl::InterfaceHandle<mozart::ViewAssociate> associate;
  MockViewAssociate mock_view_associate;
  fidl::Binding<mozart::ViewAssociate> view_associate_binding(
      &mock_view_associate, associate.NewRequest());

  // Call ViewManager::RegisterViewAssociate. MockViewAssociate::Connect
  // should be called back
  EXPECT_EQ(0, mock_view_associate.connect_invokecount);
  mozart::ViewAssociateOwnerPtr view_associate_owner;
  g_view_manager->RegisterViewAssociate(std::move(associate),
                                        view_associate_owner.NewRequest(),
                                        "test_view_associate");

  RUN_MESSAGE_LOOP_WHILE(mock_view_associate.connect_invokecount != 1);

  EXPECT_EQ(1, mock_view_associate.connect_invokecount);
}

TEST_F(ViewManagerTest, DisconnectAMockViewAssociate) {
  mozart::ViewAssociateOwnerPtr view_associate_owner;
  int owner_connection_error_callback_invokecount = 0;

  {
    // Create and bind a MockViewAssociate
    fidl::InterfaceHandle<mozart::ViewAssociate> associate;
    MockViewAssociate mock_view_associate;
    fidl::Binding<mozart::ViewAssociate> view_associate_binding(
        &mock_view_associate, associate.NewRequest());

    // Call ViewManager::RegisterViewAssociate. MockViewAssociate::Connect
    // should be called back
    EXPECT_EQ(0, mock_view_associate.connect_invokecount);

    g_view_manager->RegisterViewAssociate(std::move(associate),
                                          view_associate_owner.NewRequest(),
                                          "test_view_associate_xyz");

    // set a callback for errors
    view_associate_owner.set_connection_error_handler(
        // use lambda function as callback
        [&owner_connection_error_callback_invokecount]() {
          owner_connection_error_callback_invokecount++;
        });

    RUN_MESSAGE_LOOP_WHILE(mock_view_associate.connect_invokecount != 1);

    EXPECT_EQ(1, mock_view_associate.connect_invokecount);

    EXPECT_EQ(0, owner_connection_error_callback_invokecount);
  }

  // mock_view_associate is out of scope, should be destroyed
  // we expect to get a connection error from the owner
  RUN_MESSAGE_LOOP_WHILE(owner_connection_error_callback_invokecount != 1);

  EXPECT_EQ(1, owner_connection_error_callback_invokecount);
}

TEST_F(ViewManagerTest, DisconnectAViewAssociateOwner) {
  // Create and bind a MockViewAssociate
  fidl::InterfaceHandle<mozart::ViewAssociate> associate;
  MockViewAssociate mock_view_associate;
  fidl::Binding<mozart::ViewAssociate> view_associate_binding(
      &mock_view_associate, associate.NewRequest());

  // set a callback for errors
  int connection_error_callback_invokecount = 0;
  view_associate_binding.set_connection_error_handler(
      // use lambda function as callback
      [&connection_error_callback_invokecount]() {
        connection_error_callback_invokecount++;
      });

  {
    mozart::ViewAssociateOwnerPtr view_associate_owner;

    // Call ViewManager::RegisterViewAssociate. MockViewAssociate::Connect
    // should be called back
    EXPECT_EQ(0, mock_view_associate.connect_invokecount);

    g_view_manager->RegisterViewAssociate(std::move(associate),
                                          view_associate_owner.NewRequest(),
                                          "test_view_associate_xyz");

    RUN_MESSAGE_LOOP_WHILE(mock_view_associate.connect_invokecount != 1);

    EXPECT_EQ(1, mock_view_associate.connect_invokecount);

    EXPECT_EQ(0, connection_error_callback_invokecount);
  }

  // view_associate_owner is out of scope, should be destroyed
  // we expect to get a connection error from the view associate
  RUN_MESSAGE_LOOP_WHILE(connection_error_callback_invokecount != 1);

  EXPECT_EQ(1, connection_error_callback_invokecount);
}

}  // namespace test
}  // namespace view_manager
