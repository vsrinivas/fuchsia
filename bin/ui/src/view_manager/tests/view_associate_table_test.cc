// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_associate_table.h"

#include "apps/mozart/lib/view_framework/associates/mock_view_inspector.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "apps/mozart/src/view_manager/tests/mock_view_associate.h"
#include "apps/mozart/src/view_manager/tests/view_manager_test_base.h"

namespace view_manager {
namespace test {

class ViewAssociateTableTest : public ViewManagerTestBase {
 public:
  ViewAssociateTableTest() {}
  ~ViewAssociateTableTest() override {}

  void SetUp() override { ViewManagerTestBase::SetUp(); }

 private:
  DISALLOW_COPY_AND_ASSIGN(ViewAssociateTableTest);
};

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
        &mock_view_associate, fidl::GetProxy(&associate));

    // call ViewAssociateTable::RegisterViewAssociate
    EXPECT_EQ((size_t)0, view_associate_table.associate_count());

    mozart::ViewAssociateOwnerPtr view_associate_owner;
    view_associate_table.RegisterViewAssociate(
        &mock_view_inspector, std::move(associate),
        fidl::GetProxy(&view_associate_owner), "test_view_associate");
    KICK_MESSAGE_LOOP_WHILE(view_associate_table.associate_count() != 1);
    EXPECT_EQ((size_t)1, view_associate_table.associate_count());
  }

  // ViewAssociate has been destroyed (since it's out of scope now)
  // Make sure it's been removed
  KICK_MESSAGE_LOOP_WHILE(view_associate_table.associate_count() != 0);
  EXPECT_EQ((size_t)0, view_associate_table.associate_count());
}

}  // namespace test
}  // namespace view_manager
