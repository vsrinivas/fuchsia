// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_provider/public/commit.h"

#include <tuple>
#include <utility>

namespace cloud_provider_firebase {

Commit::Commit() = default;

Commit::Commit(CommitId id, Data content)
    : id(std::move(id)), content(std::move(content)) {}

Commit::~Commit() = default;

Commit::Commit(Commit&& other) = default;

Commit& Commit::operator=(Commit&& other) = default;

bool Commit::operator==(const Commit& other) const {
  return std::tie(id, content) == std::tie(other.id, other.content);
}

Commit Commit::Clone() const {
  Commit clone;
  clone.id = id;
  clone.content = content;
  return clone;
}

}  // namespace cloud_provider_firebase
