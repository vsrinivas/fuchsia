// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/leveldb.h"

#include <memory>

#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/storage/public/db_unittest.h"

namespace storage {
namespace {

class LevelDbTestFactory : public DbTestFactory {
 public:
  LevelDbTestFactory() = default;

  std::unique_ptr<Db> GetDb(ledger::Environment* environment,
                            ledger::ScopedTmpLocation* tmp_location) override {
    ledger::DetachedPath db_path = tmp_location->path().SubPath("db");
    auto db =
        std::make_unique<LevelDb>(environment->file_system(), environment->dispatcher(), db_path);
    Status status = db->Init();
    if (status != Status::OK)
      return nullptr;
    return std::move(db);
  }
};

INSTANTIATE_TEST_SUITE_P(LevelDbTest, DbTest,
                         testing::Values([] { return std::make_unique<LevelDbTestFactory>(); }));
}  // namespace
}  // namespace storage
