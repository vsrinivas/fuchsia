// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_DB_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_DB_H_

#include <lib/async/dispatcher.h>

#include <map>

#include "src/ledger/bin/storage/public/db.h"

namespace storage {
namespace fake {

class FakeDb : public Db {
 public:
  explicit FakeDb(async_dispatcher_t* dispatcher);
  FakeDb(const FakeDb&) = delete;
  FakeDb& operator=(const FakeDb&) = delete;
  ~FakeDb() override;

  // Db:
  Status StartBatch(coroutine::CoroutineHandler* handler, std::unique_ptr<Batch>* batch) override;
  Status Get(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
             std::string* value) override;
  Status HasKey(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) override;
  Status HasPrefix(coroutine::CoroutineHandler* handler,
                   convert::ExtendedStringView prefix) override;
  Status GetObject(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
                   ObjectIdentifier object_identifier,
                   std::unique_ptr<const Piece>* piece) override;
  Status GetByPrefix(coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
                     std::vector<std::string>* key_suffixes) override;
  Status GetEntriesByPrefix(coroutine::CoroutineHandler* handler,
                            convert::ExtendedStringView prefix,
                            std::vector<std::pair<std::string, std::string>>* entries) override;
  Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::unique_ptr<
          Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>*
          iterator) override;

 private:
  async_dispatcher_t* const dispatcher_;

  std::map<std::string, std::string> key_value_store_;
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_DB_H_
