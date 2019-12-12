// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_availability_manager.h"

#include "gtest/gtest.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/callback/set_when_called.h"

namespace ledger {
namespace {

using PageAvailabilityManagerTest = TestWithEnvironment;

TEST_F(PageAvailabilityManagerTest, PageAvailableByDefault) {
  bool on_discardable_called;
  bool on_available_called;

  PageAvailabilityManager page_availability_manager;
  page_availability_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  page_availability_manager.OnPageAvailable(SetWhenCalled(&on_available_called));

  EXPECT_TRUE(page_availability_manager.IsDiscardable());
  EXPECT_TRUE(on_available_called);
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(PageAvailabilityManagerTest, BusyPage) {
  bool on_discardable_called;
  bool on_available_called;

  PageAvailabilityManager page_availability_manager;
  page_availability_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  page_availability_manager.MarkPageBusy();
  page_availability_manager.OnPageAvailable(SetWhenCalled(&on_available_called));

  EXPECT_FALSE(page_availability_manager.IsDiscardable());
  EXPECT_FALSE(on_available_called);
  EXPECT_FALSE(on_discardable_called);
}

TEST_F(PageAvailabilityManagerTest, PageAvailabilityManagerReusable) {
  bool on_discardable_called;
  bool first_on_available_called;
  bool second_on_available_called;

  PageAvailabilityManager page_availability_manager;
  page_availability_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  page_availability_manager.MarkPageBusy();
  page_availability_manager.OnPageAvailable(SetWhenCalled(&first_on_available_called));

  EXPECT_FALSE(page_availability_manager.IsDiscardable());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(on_discardable_called);

  page_availability_manager.OnPageAvailable(SetWhenCalled(&second_on_available_called));
  EXPECT_FALSE(page_availability_manager.IsDiscardable());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(second_on_available_called);
  EXPECT_FALSE(on_discardable_called);

  page_availability_manager.MarkPageAvailable();

  EXPECT_TRUE(page_availability_manager.IsDiscardable());
  EXPECT_TRUE(first_on_available_called);
  EXPECT_TRUE(second_on_available_called);
  EXPECT_TRUE(on_discardable_called);

  page_availability_manager.SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  page_availability_manager.MarkPageBusy();
  page_availability_manager.OnPageAvailable(SetWhenCalled(&second_on_available_called));
  page_availability_manager.OnPageAvailable(SetWhenCalled(&first_on_available_called));

  EXPECT_FALSE(page_availability_manager.IsDiscardable());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(second_on_available_called);
  EXPECT_FALSE(on_discardable_called);

  page_availability_manager.MarkPageAvailable();

  EXPECT_TRUE(page_availability_manager.IsDiscardable());
  EXPECT_TRUE(first_on_available_called);
  EXPECT_TRUE(second_on_available_called);
  EXPECT_TRUE(on_discardable_called);
}

TEST_F(PageAvailabilityManagerTest, CallbacksNotCalledOnDestruction) {
  bool on_discardable_called;
  bool first_on_available_called;
  bool second_on_available_called;

  auto page_availability_manager = std::make_unique<PageAvailabilityManager>();
  page_availability_manager->SetOnDiscardable(SetWhenCalled(&on_discardable_called));
  page_availability_manager->MarkPageBusy();
  page_availability_manager->OnPageAvailable(SetWhenCalled(&first_on_available_called));

  EXPECT_FALSE(page_availability_manager->IsDiscardable());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(on_discardable_called);

  page_availability_manager->OnPageAvailable(SetWhenCalled(&second_on_available_called));

  EXPECT_FALSE(page_availability_manager->IsDiscardable());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(second_on_available_called);
  EXPECT_FALSE(on_discardable_called);

  page_availability_manager.reset();

  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(second_on_available_called);
  EXPECT_FALSE(on_discardable_called);
}

}  // namespace
}  // namespace ledger
