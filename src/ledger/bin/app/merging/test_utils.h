// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_MERGING_TEST_UTILS_H_
#define SRC_LEDGER_BIN_APP_MERGING_TEST_UTILS_H_

#include <lib/fit/function.h>

#include <functional>
#include <memory>

#include "gtest/gtest.h"
#include "src/ledger/bin/encryption/fake/fake_encryption_service.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/storage/public/journal.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/testing/test_with_environment.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace ledger {

class TestWithPageStorage : public TestWithEnvironment {
 public:
  TestWithPageStorage();
  ~TestWithPageStorage() override;

 protected:
  virtual storage::PageStorage* page_storage() = 0;

  // Returns a function that, when executed, adds the provided key and object to
  // a journal.
  fit::function<void(storage::Journal*)> AddKeyValueToJournal(const std::string& key,
                                                              std::string value);

  // Returns a function that, when executed, deleted the provided key from a
  // journal.
  fit::function<void(storage::Journal*)> DeleteKeyFromJournal(const std::string& key);

  ::testing::AssertionResult GetValue(storage::ObjectIdentifier object_identifier,
                                      std::string* value);

  ::testing::AssertionResult CreatePageStorage(std::unique_ptr<storage::PageStorage>* page_storage);

  fit::closure MakeQuitTaskOnce();

 private:
  std::unique_ptr<ScopedTmpLocation> tmp_location_;
  encryption::FakeEncryptionService encryption_service_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_MERGING_TEST_UTILS_H_
