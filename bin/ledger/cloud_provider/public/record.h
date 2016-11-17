// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_RECORD_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_RECORD_H_

#include "apps/ledger/src/cloud_provider/public/commit.h"
#include "lib/ftl/macros.h"

namespace cloud_provider {

// Represents a commit along with its timestamp.
struct Record {
  Record();
  Record(Commit n, std::string t);

  ~Record();

  Record(Record&&);
  Record& operator=(Record&&);

  Commit commit;
  std::string timestamp;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Record);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_RECORD_H_
