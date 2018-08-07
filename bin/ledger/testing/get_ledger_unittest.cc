// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/get_ledger.h"

#include <lib/async-loop/cpp/loop.h>

#include "garnet/public/lib/callback/capture.h"
#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace {

TEST(GetLedgerTest, CreateAndDeleteLedger) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  scoped_tmpfs::ScopedTmpFS tmpfs;

  auto startup_context = component::StartupContext::CreateFromStartupInfo();
  fuchsia::sys::ComponentControllerPtr controller;

  Status status = Status::UNKNOWN_ERROR;
  LedgerPtr ledger;

  GetLedger(startup_context.get(), controller.NewRequest(), nullptr,
            "ledger_name", DetachedPath(tmpfs.root_fd()), [&] { loop.Quit(); },
            callback::Capture([&] { loop.Quit(); }, &status, &ledger));
  loop.Run();

  EXPECT_EQ(Status::OK, status);

  KillLedgerProcess(&controller);
}

TEST(GetLedgerTest, GetPageEnsureInitialized) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  scoped_tmpfs::ScopedTmpFS tmpfs;

  auto startup_context = component::StartupContext::CreateFromStartupInfo();
  fuchsia::sys::ComponentControllerPtr controller;

  Status status = Status::UNKNOWN_ERROR;
  LedgerPtr ledger;

  GetLedger(startup_context.get(), controller.NewRequest(), nullptr,
            "ledger_name", DetachedPath(tmpfs.root_fd()), [&] { loop.Quit(); },
            callback::Capture([&] { loop.Quit(); }, &status, &ledger));
  loop.Run();
  loop.ResetQuit();

  ASSERT_EQ(Status::OK, status);

  status = Status::UNKNOWN_ERROR;
  PagePtr page;
  PageId page_id;

  GetPageEnsureInitialized(
      &ledger, nullptr, [&] { loop.Quit(); },
      callback::Capture([&] { loop.Quit(); }, &status, &page, &page_id));
  loop.Run();

  EXPECT_EQ(Status::OK, status);

  KillLedgerProcess(&controller);
}

}  // namespace
}  // namespace ledger
