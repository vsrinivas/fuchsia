// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_associate_table.h"

#include "apps/mozart/lib/view_associate_framework/mock_view_inspector.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "apps/mozart/src/view_manager/tests/mocks/mock_view_associate.h"
#include "apps/mozart/src/view_manager/tests/test_with_message_loop.h"

namespace view_manager {
namespace test {

class ViewAssociateTableTest : public TestWithMessageLoop {};

TEST_F(ViewAssociateTableTest, RegisterViewAssociateThenCloseIt) {
  // Create a mock view registry
  mozart::MockViewInspector mock_view_inspector;

  // Create a view associate table
  ViewAssociateTable view_associate_table;

  {
    // Create and bind a MockViewAssociate
    mozart::ViewAssociatePtr associate;
    MockViewAssociate mock_view_associate;
    fidl::Binding<mozart::ViewAssociate> view_associate_binding(
        &mock_view_associate, associate.NewRequest());

    // call ViewAssociateTable::RegisterViewAssociate
    EXPECT_EQ((size_t)0, view_associate_table.associate_count());

    mozart::ViewAssociateOwnerPtr view_associate_owner;
    view_associate_table.RegisterViewAssociate(
        &mock_view_inspector, std::move(associate),
        view_associate_owner.NewRequest(), "test_view_associate");
    RUN_MESSAGE_LOOP_WHILE(view_associate_table.associate_count() != 1);
    EXPECT_EQ((size_t)1, view_associate_table.associate_count());
  }

  // ViewAssociate has been destroyed (since it's out of scope now)
  // Make sure it's been removed
  RUN_MESSAGE_LOOP_WHILE(view_associate_table.associate_count() != 0);
  EXPECT_EQ((size_t)0, view_associate_table.associate_count());
}

TEST_F(ViewAssociateTableTest, MultipleViewAssociates) {
  // Create a mock view registry
  mozart::MockViewInspector mock_view_inspector;

  // Create a view associate table
  ViewAssociateTable view_associate_table;

  {
    // Create and bind a MockViewAssociate
    mozart::ViewAssociatePtr associate_one;
    MockViewAssociate mock_view_associate_one;
    fidl::Binding<mozart::ViewAssociate> view_associate_binding_one(
        &mock_view_associate_one, associate_one.NewRequest());

    // call ViewAssociateTable::RegisterViewAssociate
    EXPECT_EQ((size_t)0, view_associate_table.associate_count());

    mozart::ViewAssociateOwnerPtr view_associate_owner_one;
    view_associate_table.RegisterViewAssociate(
        &mock_view_inspector, std::move(associate_one),
        view_associate_owner_one.NewRequest(), "test_view_associate_one");
    RUN_MESSAGE_LOOP_WHILE(view_associate_table.associate_count() != 1);
    EXPECT_EQ((size_t)1, view_associate_table.associate_count());


    // Create and bind a second MockViewAssociate
    mozart::ViewAssociatePtr associate_two;
    MockViewAssociate mock_view_associate_two;
    fidl::Binding<mozart::ViewAssociate> view_associate_binding_two(
        &mock_view_associate_two, associate_two.NewRequest());

    // call ViewAssociateTable::RegisterViewAssociate
    EXPECT_EQ((size_t)1, view_associate_table.associate_count());

    mozart::ViewAssociateOwnerPtr view_associate_owner_two;
    view_associate_table.RegisterViewAssociate(
        &mock_view_inspector, std::move(associate_two),
        view_associate_owner_two.NewRequest(), "test_view_associate_two");
    RUN_MESSAGE_LOOP_WHILE(view_associate_table.associate_count() != 2);
    EXPECT_EQ((size_t)2, view_associate_table.associate_count());
  }

  // The ViewAssociates have been destroyed (since they're out of scope now)
  // Make sure they've been removed
  RUN_MESSAGE_LOOP_WHILE(view_associate_table.associate_count() != 0);
  EXPECT_EQ((size_t)0, view_associate_table.associate_count());
}

}  // namespace test
}  // namespace view_manager
