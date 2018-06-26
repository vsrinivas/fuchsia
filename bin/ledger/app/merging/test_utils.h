// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_MERGING_TEST_UTILS_H_
#define PERIDOT_BIN_LEDGER_APP_MERGING_TEST_UTILS_H_

#include <functional>
#include <memory>

#include "gtest/gtest.h"
#include "lib/backoff/backoff.h"
#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"
#include "peridot/bin/ledger/encryption/fake/fake_encryption_service.h"
#include "peridot/bin/ledger/storage/public/journal.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace ledger {
namespace test {
// Dummy implementation of a backoff policy, which always returns zero backoff
// time..
class TestBackoff : public backoff::Backoff {
 public:
  explicit TestBackoff(int* get_next_count);
  ~TestBackoff() override;

  zx::duration GetNext() override;

  void Reset() override;

  int* get_next_count_;
};

class TestWithPageStorage : public gtest::TestLoopFixture {
 public:
  TestWithPageStorage();
  ~TestWithPageStorage() override;

 protected:
  virtual storage::PageStorage* page_storage() = 0;

  // Returns a function that, when executed, adds the provided key and object to
  // a journal.
  std::function<void(storage::Journal*)> AddKeyValueToJournal(
      const std::string& key, std::string value);

  // Returns a function that, when executed, deleted the provided key from a
  // journal.
  std::function<void(storage::Journal*)> DeleteKeyFromJournal(
      const std::string& key);

  ::testing::AssertionResult GetValue(
      storage::ObjectIdentifier object_identifier, std::string* value);

  ::testing::AssertionResult CreatePageStorage(
      std::unique_ptr<storage::PageStorage>* page_storage);

  fxl::Closure MakeQuitTaskOnce();

 private:
  ScopedTmpFS tmpfs_;
  coroutine::CoroutineServiceImpl coroutine_service_;
  encryption::FakeEncryptionService encryption_service_;
};

}  // namespace test
}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_MERGING_TEST_UTILS_H_
