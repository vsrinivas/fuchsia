// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/get_ledger.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/lib/callback/capture.h"

namespace ledger {
namespace {

TEST(GetLedgerTest, CreateAndDeleteLedger) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<Platform> platform = MakePlatform();
  std::unique_ptr<ScopedTmpLocation> tmp_location =
      platform->file_system()->CreateScopedTmpLocation();

  auto component_context = sys::ComponentContext::Create();
  fuchsia::sys::ComponentControllerPtr controller;

  LedgerPtr ledger;
  fit::function<void(fit::closure)> close_repository;
  Status status = GetLedger(
      component_context.get(), controller.NewRequest(), nullptr, "", "ledger_name",
      tmp_location->path(), [&] { loop.Quit(); }, &ledger, kTestingGarbageCollectionPolicy,
      &close_repository);

  // No need to |Sync| as |GetLedger| handles it.
  EXPECT_EQ(status, Status::OK);

  ledger.Unbind();
  close_repository([&] { loop.Quit(); });
  loop.Run();

  KillLedgerProcess(&controller);
}

TEST(GetLedgerTest, GetPageEnsureInitialized) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<Platform> platform = MakePlatform();
  std::unique_ptr<ScopedTmpLocation> tmp_location =
      platform->file_system()->CreateScopedTmpLocation();

  auto component_context = sys::ComponentContext::Create();
  fuchsia::sys::ComponentControllerPtr controller;

  LedgerPtr ledger;
  fit::function<void(fit::closure)> close_repository;
  Status status = GetLedger(
      component_context.get(), controller.NewRequest(), nullptr, "", "ledger_name",
      tmp_location->path(), [&] { loop.Quit(); }, &ledger, kTestingGarbageCollectionPolicy,
      &close_repository);

  ASSERT_EQ(status, Status::OK);

  status = Status::INTERNAL_ERROR;
  PagePtr page;
  PageId page_id;

  GetPageEnsureInitialized(
      &ledger, nullptr, DelayCallback::NO, [&] { loop.Quit(); },
      callback::Capture([&] { loop.Quit(); }, &status, &page, &page_id));
  loop.Run();

  EXPECT_EQ(status, Status::OK);

  page.Unbind();
  ledger.Unbind();
  close_repository([&] { loop.Quit(); });
  loop.Run();

  KillLedgerProcess(&controller);
}

}  // namespace
}  // namespace ledger
