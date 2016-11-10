// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_provider/public/commit.h"

#include <utility>

namespace cloud_provider {

Commit::Commit() = default;

Commit::Commit(CommitId&& id,
               Data&& content,
               std::map<ObjectId, Data>&& storage_objects)
    : id(std::move(id)),
      content(std::move(content)),
      storage_objects(std::move(storage_objects)) {}

Commit::~Commit() = default;

Commit::Commit(Commit&&) = default;

Commit& Commit::operator=(Commit&&) = default;

bool Commit::operator==(const Commit& other) const {
  return id == other.id && content == other.content &&
         storage_objects == other.storage_objects;
}

Commit Commit::Clone() const {
  Commit clone;
  clone.id = id;
  clone.content = content;
  clone.storage_objects = storage_objects;
  return clone;
}

}  // namespace cloud_provider
