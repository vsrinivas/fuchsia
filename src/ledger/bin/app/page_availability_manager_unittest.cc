// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_availability_manager.h"

#include <map>
#include <random>
#include <vector>

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/callback/set_when_called.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

using PageAvailabilityManagerTest = TestWithEnvironment;

TEST_F(PageAvailabilityManagerTest, PageAvailableByDefault) {
  storage::PageId page_id = std::string(::fuchsia::ledger::kPageIdSize, '4');
  bool on_empty_called;
  bool on_available_called;

  PageAvailabilityManager page_availability_manager;
  page_availability_manager.set_on_empty(
      callback::SetWhenCalled(&on_empty_called));
  page_availability_manager.OnPageAvailable(
      page_id, callback::SetWhenCalled(&on_available_called));

  EXPECT_TRUE(page_availability_manager.IsEmpty());
  EXPECT_TRUE(on_available_called);
  EXPECT_FALSE(on_empty_called);
}

TEST_F(PageAvailabilityManagerTest, SingleBusyPage) {
  storage::PageId page_id = std::string(::fuchsia::ledger::kPageIdSize, '4');
  bool on_empty_called;
  bool on_available_called;

  PageAvailabilityManager page_availability_manager;
  page_availability_manager.set_on_empty(
      callback::SetWhenCalled(&on_empty_called));
  page_availability_manager.MarkPageBusy(page_id);
  page_availability_manager.OnPageAvailable(
      page_id, callback::SetWhenCalled(&on_available_called));

  EXPECT_FALSE(page_availability_manager.IsEmpty());
  EXPECT_FALSE(on_available_called);
  EXPECT_FALSE(on_empty_called);
}

TEST_F(PageAvailabilityManagerTest, MultiplePages) {
  auto bit_generator = environment_.random()->NewBitGenerator<size_t>();
  size_t page_count = std::uniform_int_distribution(2u, 20u)(bit_generator);
  // The number of callbacks per page to be registered with the
  // PageAvailabilityManager.
  int dimension_count = std::uniform_int_distribution(2, 10)(bit_generator);
  bool page_available_called[page_count][2];
  std::vector<storage::PageId> page_ids;
  std::map<storage::PageId, size_t> indices;
  for (uint8_t i = 0; i < page_count; i++) {
    storage::PageId page_id =
        std::string(::fuchsia::ledger::kPageIdSize, i + 'a');
    page_ids.push_back(page_id);
    indices[page_id] = i;
  }
  bool on_empty_called;

  PageAvailabilityManager page_availability_manager;
  page_availability_manager.set_on_empty(
      callback::SetWhenCalled(&on_empty_called));
  EXPECT_TRUE(page_availability_manager.IsEmpty());
  EXPECT_FALSE(on_empty_called);
  // In a random order, mark all the pages busy.
  std::vector<storage::PageId> remaining_page_ids = page_ids;
  std::shuffle(remaining_page_ids.begin(), remaining_page_ids.end(),
               bit_generator);
  while (!remaining_page_ids.empty()) {
    auto page_id = remaining_page_ids.back();
    page_availability_manager.MarkPageBusy(page_id);
    remaining_page_ids.pop_back();
  }
  EXPECT_FALSE(on_empty_called);
  // As many times as the number of callbacks to be registered for a page...
  for (int dimension = 0; dimension < dimension_count; dimension++) {
    // ... register a single callback for each page (in a random page ordering)
    remaining_page_ids = page_ids;
    std::shuffle(remaining_page_ids.begin(), remaining_page_ids.end(),
                 bit_generator);
    while (!remaining_page_ids.empty()) {
      auto page_id = remaining_page_ids.back();
      page_availability_manager.OnPageAvailable(
          page_id, callback::SetWhenCalled(
                       &page_available_called[indices[page_id]][dimension]));
      EXPECT_FALSE(page_available_called[indices[page_id]][dimension]);
      remaining_page_ids.pop_back();
    }
  }
  // In a random order, mark all the pages available and verify that each page's
  // callbacks are called.
  remaining_page_ids = page_ids;
  std::shuffle(remaining_page_ids.begin(), remaining_page_ids.end(),
               bit_generator);
  while (!remaining_page_ids.empty()) {
    EXPECT_FALSE(on_empty_called);
    auto page_id = remaining_page_ids.back();
    page_availability_manager.MarkPageAvailable(page_id);
    for (int dimension = 0; dimension < dimension_count; dimension++) {
      EXPECT_TRUE(page_available_called[indices[page_id]][dimension]);
    }
    remaining_page_ids.pop_back();
  }
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageAvailabilityManagerTest, PageAvailabilityManagerReusable) {
  storage::PageId first_page_id =
      std::string(::fuchsia::ledger::kPageIdSize, '8');
  storage::PageId second_page_id =
      std::string(::fuchsia::ledger::kPageIdSize, '9');
  bool on_empty_called;
  bool first_on_available_called;
  bool second_on_available_called;

  PageAvailabilityManager page_availability_manager;
  page_availability_manager.set_on_empty(
      callback::SetWhenCalled(&on_empty_called));
  page_availability_manager.MarkPageBusy(first_page_id);
  page_availability_manager.OnPageAvailable(
      first_page_id, callback::SetWhenCalled(&first_on_available_called));

  EXPECT_FALSE(page_availability_manager.IsEmpty());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(on_empty_called);

  page_availability_manager.MarkPageBusy(second_page_id);
  page_availability_manager.OnPageAvailable(
      second_page_id, callback::SetWhenCalled(&second_on_available_called));

  EXPECT_FALSE(page_availability_manager.IsEmpty());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(second_on_available_called);
  EXPECT_FALSE(on_empty_called);

  page_availability_manager.MarkPageAvailable(second_page_id);
  page_availability_manager.MarkPageAvailable(first_page_id);

  EXPECT_TRUE(page_availability_manager.IsEmpty());
  EXPECT_TRUE(first_on_available_called);
  EXPECT_TRUE(second_on_available_called);
  EXPECT_TRUE(on_empty_called);

  on_empty_called = false;
  first_on_available_called = false;
  second_on_available_called = false;

  page_availability_manager.MarkPageBusy(second_page_id);
  page_availability_manager.MarkPageBusy(first_page_id);
  page_availability_manager.OnPageAvailable(
      second_page_id, callback::SetWhenCalled(&second_on_available_called));
  page_availability_manager.OnPageAvailable(
      first_page_id, callback::SetWhenCalled(&first_on_available_called));

  EXPECT_FALSE(page_availability_manager.IsEmpty());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(second_on_available_called);
  EXPECT_FALSE(on_empty_called);

  page_availability_manager.MarkPageAvailable(first_page_id);
  page_availability_manager.MarkPageAvailable(second_page_id);

  EXPECT_TRUE(page_availability_manager.IsEmpty());
  EXPECT_TRUE(first_on_available_called);
  EXPECT_TRUE(second_on_available_called);
  EXPECT_TRUE(on_empty_called);
}

TEST_F(PageAvailabilityManagerTest, CallbacksNotCalledOnDestruction) {
  storage::PageId first_page_id =
      std::string(::fuchsia::ledger::kPageIdSize, '8');
  storage::PageId second_page_id =
      std::string(::fuchsia::ledger::kPageIdSize, '9');
  bool on_empty_called;
  bool first_on_available_called;
  bool second_on_available_called;

  auto page_availability_manager = std::make_unique<PageAvailabilityManager>();
  page_availability_manager->set_on_empty(
      callback::SetWhenCalled(&on_empty_called));
  page_availability_manager->MarkPageBusy(first_page_id);
  page_availability_manager->OnPageAvailable(
      first_page_id, callback::SetWhenCalled(&first_on_available_called));

  EXPECT_FALSE(page_availability_manager->IsEmpty());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(on_empty_called);

  page_availability_manager->MarkPageBusy(second_page_id);
  page_availability_manager->OnPageAvailable(
      second_page_id, callback::SetWhenCalled(&second_on_available_called));

  EXPECT_FALSE(page_availability_manager->IsEmpty());
  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(second_on_available_called);
  EXPECT_FALSE(on_empty_called);

  page_availability_manager.reset();

  EXPECT_FALSE(first_on_available_called);
  EXPECT_FALSE(second_on_available_called);
  EXPECT_FALSE(on_empty_called);
}

}  // namespace
}  // namespace ledger
