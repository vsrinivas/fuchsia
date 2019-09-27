// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_UNITTEST_H_
#define SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_UNITTEST_H_

#include <lib/async/dispatcher.h>

#include <functional>
#include <memory>

#include "gtest/gtest.h"
#include "lib/fit/function.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/storage/public/db.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/ledger/lib/coroutine/coroutine.h"

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

 protected:
  void SetUp() override {
    ledger::TestWithEnvironment::SetUp();
    ASSERT_NE(db_, nullptr);
  }

  // Inserts a |key| associated to |value| in db_.
  void PutEntry(const std::string& key, const std::string& value);

  // Runs a test that:
  // 1) issues a read for a key,
  // 2) issues a write for the same key.
  // The read (1) and write (2) are passed as parameters |do_write| and |do_read|, to test the
  // various read and write methods of the |Db| interface. The test performs careful interleaving to
  // ensure that, although read (1) is issued before write (2), the write is given a chance to be
  // scheduled right after read yields for the first time, exposing potential ordering issues in
  // the implementation.
  // |do_write| must leave the provided batch in a state that is ready to be executed.
  // |RunReadWriteTest| is responsible for issuing the write (ie. calling Execute).
  // |do_read| and |do_write| are responsible for operating on the same key and EXPECTing meaningful
  // results. Eg. if |do_read| performs a HasKey and |do_write| a Put, the former should expect
  // NOT_FOUND.
  void RunReadWriteTest(fit::function<void(coroutine::CoroutineHandler*)> do_read,
                        fit::function<void(coroutine::CoroutineHandler*, Db::Batch*)> do_write);

  // Runs a test that:
  // 1) issues a write for a key,
  // 2) issues a read for the same key.
  // The write (1) and read (2) are passed as parameters |do_write| and |do_read|, to test the
  // various read and write methods of the |Db| interface. The test performs careful interleaving to
  // ensure that, although write (1) is issued before read (2), the read is given a chance to be
  // scheduled right after write yields for the first time, exposing potential ordering issues in
  // the implementation.
  // |do_write| must leave the provided batch in a state that is ready to be executed.
  // |RunWriteReadTest| is responsible for issuing the write (ie. calling Execute).
  // |do_read| and |do_write| are responsible for operating on the same key and EXPECTing meaningful
  // results. Eg. if |do_write| performs a Delete and |do_read| a HasKey, the latter should expect
  // NOT_FOUND.
  void RunWriteReadTest(fit::function<void(coroutine::CoroutineHandler*, Db::Batch*)> do_write,
                        fit::function<void(coroutine::CoroutineHandler*)> do_read);

  scoped_tmpfs::ScopedTmpFS tmpfs_;
  std::unique_ptr<Db> db_;
};

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_PUBLIC_DB_UNITTEST_H_
