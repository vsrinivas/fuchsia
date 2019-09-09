// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_UNITTEST_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_UNITTEST_H_

#include <lib/async/dispatcher.h>

#include <functional>
#include <memory>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace storage {

// This class implements Value-Parameterized Abstract Tests for the Db interface.
//
// See db_unittest.cc for the actual tests.
//
// To run the test suite, implementations need to provide a factory function
//   std::unique_ptr<Db> GetDb(scoped_tmpfs::ScopedTmpFS* tmpfs, async_dispatcher_t* dispatcher)
// and instantiate the test suite with:
//   INSTANTIATE_TEST_SUITE_P(DbImplTest, DbTest, ::testing::Values(&GetDb));
class DbTest
    : public ledger::TestWithEnvironment,
      public ::testing::WithParamInterface<
          std::function<std::unique_ptr<Db>(scoped_tmpfs::ScopedTmpFS*, async_dispatcher_t*)>> {
 public:
  DbTest() : db_(GetParam()(&tmpfs_, environment_.dispatcher())) {}

  void SetUp() override {
    ledger::TestWithEnvironment::SetUp();
    ASSERT_NE(db_, nullptr);
  }

 private:
  scoped_tmpfs::ScopedTmpFS tmpfs_;

 protected:
  std::unique_ptr<Db> db_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_UNITTEST_H_
