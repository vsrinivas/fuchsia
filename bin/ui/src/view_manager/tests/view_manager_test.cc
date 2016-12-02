// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "apps/mozart/src/view_manager/tests/mock_view_associate.h"
#include "apps/mozart/src/view_manager/tests/view_manager_test_base.h"
#include "lib/fidl/cpp/application/connect.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace view_manager {
namespace test {

class MockViewListener : public mozart::ViewListener {
 public:
  MockViewListener() {}
  ~MockViewListener() override {}

  void OnInvalidation(mozart::ViewInvalidationPtr invalidation,
                      const OnInvalidationCallback& callback) override {
    callback();
  }
};

class ViewManagerTest : public ViewManagerTestBase {
 public:
  ViewManagerTest() {}
  ~ViewManagerTest() override {}

  void SetUp() override {
    ViewManagerTestBase::SetUp();

    // Connect to view manager
    fidl::ConnectToService(shell(), "mojo:view_manager_service",
                           view_manager_.NewRequest());
  }

 protected:
  mozart::ViewManagerPtr view_manager_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ViewManagerTest);
};

TEST_F(ViewManagerTest, CreateAViewManager) {
  ASSERT_TRUE(view_manager_.is_bound());
}

TEST_F(ViewManagerTest, CreateAView) {
  // Create and bind a mock view listener
  mozart::ViewListenerPtr view_listener;
  MockViewListener mock_view_listener;
  fidl::Binding<mozart::ViewListener> view_listener_binding(
      &mock_view_listener, view_listener.NewRequest());

  // Create a view
  mozart::ViewPtr view;
  mozart::ViewOwnerPtr view_owner;
  view_manager_->CreateView(view.NewRequest(), view_owner.NewRequest(),
                            std::move(view_listener), "test_view");

  // Call View::GetToken. Check that you get the callback.
  int view_token_callback_invokecount = 0;
  auto view_token_callback = [&view_token_callback_invokecount](
      mozart::ViewTokenPtr token) { view_token_callback_invokecount++; };

  EXPECT_EQ(0, view_token_callback_invokecount);
  view->GetToken(view_token_callback);

  KICK_MESSAGE_LOOP_WHILE(view_token_callback_invokecount != 1);

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
  view_manager_->CreateView(
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
  view_manager_->CreateView(child_view.NewRequest(),
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

  KICK_MESSAGE_LOOP_WHILE(view_token_callback_invokecount != 1);

  EXPECT_EQ(1, view_token_callback_invokecount);
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
  view_manager_->RegisterViewAssociate(std::move(associate),
                                       view_associate_owner.NewRequest(),
                                       "test_view_associate");

  KICK_MESSAGE_LOOP_WHILE(mock_view_associate.connect_invokecount != 1);

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

    view_manager_->RegisterViewAssociate(std::move(associate),
                                         view_associate_owner.NewRequest(),
                                         "test_view_associate_xyz");

    // set a callback for errors
    view_associate_owner.set_connection_error_handler(
        // use lambda function as callback
        [&owner_connection_error_callback_invokecount]() {
          owner_connection_error_callback_invokecount++;
        });

    KICK_MESSAGE_LOOP_WHILE(mock_view_associate.connect_invokecount != 1);

    EXPECT_EQ(1, mock_view_associate.connect_invokecount);

    EXPECT_EQ(0, owner_connection_error_callback_invokecount);
  }

  // mock_view_associate is out of scope, should be destroyed
  // we expect to get a connection error from the owner
  KICK_MESSAGE_LOOP_WHILE(owner_connection_error_callback_invokecount != 1)

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

    view_manager_->RegisterViewAssociate(std::move(associate),
                                         view_associate_owner.NewRequest(),
                                         "test_view_associate_xyz");

    KICK_MESSAGE_LOOP_WHILE(mock_view_associate.connect_invokecount != 1);

    EXPECT_EQ(1, mock_view_associate.connect_invokecount);

    EXPECT_EQ(0, connection_error_callback_invokecount);
  }

  // view_associate_owner is out of scope, should be destroyed
  // we expect to get a connection error from the view associate
  KICK_MESSAGE_LOOP_WHILE(connection_error_callback_invokecount != 1)

  EXPECT_EQ(1, connection_error_callback_invokecount);
}

}  // namespace test
}  // namespace view_manager
