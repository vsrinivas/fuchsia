// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_DB_VIEW_FACTORY_H_
#define SRC_LEDGER_BIN_APP_DB_VIEW_FACTORY_H_

#include <algorithm>
#include <memory>
#include <string>

#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/storage/public/db.h"

namespace ledger {
// DbViewFactory creates, from a database |db|, editable views of |db| for a provided prefix. This
// prefix is invisible for clients of the returned view. |DbViewFactory| must outlive the views it
// created.
class DbViewFactory {
 public:
  explicit DbViewFactory(std::unique_ptr<storage::Db> db);
  ~DbViewFactory();

  // Create a new view prefixed by |prefix|.
  std::unique_ptr<storage::Db> CreateDbView(RepositoryRowPrefix prefix);

 private:
  std::unique_ptr<storage::Db> db_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_DB_VIEW_FACTORY_H_
