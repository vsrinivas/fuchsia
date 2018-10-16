// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_FAKE_DB_FACTORY_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_FAKE_DB_FACTORY_H_

#include "peridot/bin/ledger/storage/impl/db_factory.h"

namespace storage {

// A fake implementation of the DbFactory.
// TODO(nellyv): move this class to peridot/bin/ledger/storage/fake.
// TODO(nellyv): Use a fake implementation of Db, instead of LevelDb when the
// DbFactory API is updated.
class FakeDbFactory : public DbFactory {
 public:
  explicit FakeDbFactory(async_dispatcher_t* dispatcher)
      : dispatcher_(dispatcher) {}

  void CreateDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<LevelDb>)> callback) override;

  void GetDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<LevelDb>)> callback) override;

 private:
  async_dispatcher_t* dispatcher_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_FAKE_DB_FACTORY_H_
