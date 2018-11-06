// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_DB_FACTORY_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_DB_FACTORY_H_

#include <memory>

#include "peridot/bin/ledger/filesystem/detached_path.h"
#include "peridot/bin/ledger/storage/public/db.h"

namespace storage {

// A factory for Db instances.
class DbFactory {
 public:
  DbFactory() {}
  virtual ~DbFactory() {}

  // Opens and returns an initialized instance of Db in the given |db_path|. If
  // the Db doesn't already exist, it creates it.
  virtual void GetOrCreateDb(
      ledger::DetachedPath db_path,
      fit::function<void(Status, std::unique_ptr<Db>)> callback) = 0;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_DB_FACTORY_H_
