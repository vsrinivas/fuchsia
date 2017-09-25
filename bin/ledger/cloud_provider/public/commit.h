// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CLOUD_PROVIDER_PUBLIC_COMMIT_H_
#define PERIDOT_BIN_LEDGER_CLOUD_PROVIDER_PUBLIC_COMMIT_H_

#include <map>
#include <string>

#include "peridot/bin/ledger/cloud_provider/public/types.h"
#include "lib/fxl/macros.h"

namespace cloud_provider_firebase {

// Represents a commit.
struct Commit {
  Commit();
  Commit(CommitId id, Data content);

  ~Commit();

  Commit(Commit&& other);
  Commit& operator=(Commit&& other);

  bool operator==(const Commit& other) const;

  Commit Clone() const;

  // The commit id.
  CommitId id;

  // The commit content.
  Data content;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Commit);
};

}  // namespace cloud_provider_firebase

#endif  // PERIDOT_BIN_LEDGER_CLOUD_PROVIDER_PUBLIC_COMMIT_H_
