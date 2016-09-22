// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_LEDGER_STORAGE_H_
#define APPS_LEDGER_STORAGE_PUBLIC_LEDGER_STORAGE_H_

#include <memory>
#include <string>

#include "apps/ledger/storage/public/application_storage.h"
#include "lib/ftl/macros.h"

namespace storage {

// The |LedgerStorage| manages all data access for the ledger. An instance of
// this object must remain alive for all objects transitively created by it to
// remain valid.
class LedgerStorage {
 public:
  LedgerStorage() {}
  virtual ~LedgerStorage() {}

  // Creates a new |Storage| for a given identity.
  virtual std::unique_ptr<ApplicationStorage> CreateApplicationStorage(
      std::string identity) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerStorage);
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_PUBLIC_LEDGER_STORAGE_H_
