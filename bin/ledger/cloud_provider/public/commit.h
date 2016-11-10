// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_COMMIT_H_
#define APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_COMMIT_H_

#include <map>
#include <string>

#include "apps/ledger/src/cloud_provider/public/types.h"
#include "lib/ftl/macros.h"

namespace cloud_provider {

// Represents a commit.
struct Commit {
  Commit();
  Commit(CommitId&& id,
         Data&& content,
         std::map<ObjectId, Data>&& inline_storage_objects);

  ~Commit();

  Commit(Commit&&);
  Commit& operator=(Commit&&);

  bool operator==(const Commit& other) const;

  Commit Clone() const;

  // The commit id.
  CommitId id;

  // The commit content.
  Data content;

  // The inline storage objects.
  std::map<ObjectId, Data> storage_objects;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Commit);
};

}  // namespace cloud_provider

#endif  // APPS_LEDGER_SRC_CLOUD_PROVIDER_PUBLIC_COMMIT_H_
