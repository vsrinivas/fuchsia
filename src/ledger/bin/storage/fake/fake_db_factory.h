// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_DB_FACTORY_H_
#define SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_DB_FACTORY_H_

#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/storage/public/db_factory.h"

namespace storage {
namespace fake {

// A fake implementation of the DbFactory.
class FakeDbFactory : public DbFactory {
 public:
  explicit FakeDbFactory(ledger::FileSystem* file_system, async_dispatcher_t* dispatcher)
      : file_system_(file_system), dispatcher_(dispatcher) {}

  void GetOrCreateDb(ledger::DetachedPath db_path, DbFactory::OnDbNotFound on_db_not_found,
                     fit::function<void(Status, std::unique_ptr<Db>)> callback) override;

 private:
  void CreateInitializedDb(fit::function<void(Status, std::unique_ptr<Db>)> callback);

  ledger::FileSystem* file_system_;
  async_dispatcher_t* dispatcher_;
};

}  // namespace fake
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_FAKE_FAKE_DB_FACTORY_H_
