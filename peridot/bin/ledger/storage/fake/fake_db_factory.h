// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_DB_FACTORY_H_
#define PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_DB_FACTORY_H_

#include "peridot/bin/ledger/storage/public/db_factory.h"

namespace storage {
namespace fake {

// A fake implementation of the DbFactory.
class FakeDbFactory : public DbFactory {
 public:
  explicit FakeDbFactory(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher) {}

  void GetOrCreateDb(
      ledger::DetachedPath db_path, DbFactory::OnDbNotFound on_db_not_found,
      fit::function<void(Status, std::unique_ptr<Db>)> callback) override;

 private:
  void CreateInitializedDb(
      fit::function<void(Status, std::unique_ptr<Db>)> callback);

  async_dispatcher_t* dispatcher_;
};

}  // namespace fake
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_FAKE_FAKE_DB_FACTORY_H_
