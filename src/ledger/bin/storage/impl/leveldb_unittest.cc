// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/leveldb.h"

#include <memory>

#include "src/ledger/bin/filesystem/detached_path.h"
#include "src/ledger/bin/storage/public/db_unittest.h"

namespace storage {
namespace {

class LevelDbTestFactory : public DbTestFactory {
 public:
  LevelDbTestFactory() = default;

  std::unique_ptr<Db> GetDb(scoped_tmpfs::ScopedTmpFS* tmpfs,
                            async_dispatcher_t* dispatcher) override {
    ledger::DetachedPath db_path(tmpfs->root_fd(), "db");
    auto db = std::make_unique<LevelDb>(dispatcher, db_path);
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
