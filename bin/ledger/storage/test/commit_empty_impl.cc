// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/test/commit_empty_impl.h"

#include "lib/ftl/logging.h"

namespace storage {
namespace test {

std::unique_ptr<Commit> CommitEmptyImpl::Clone() const {
  FTL_NOTIMPLEMENTED();
  return nullptr;
}

const CommitId& CommitEmptyImpl::GetId() const {
  static std::string id = "NOT_IMPLEMENTED";
  FTL_NOTIMPLEMENTED();
  return id;
}

std::vector<CommitIdView> CommitEmptyImpl::GetParentIds() const {
  FTL_NOTIMPLEMENTED();
  return {};
}

int64_t CommitEmptyImpl::GetTimestamp() const {
  FTL_NOTIMPLEMENTED();
  return 0;
}

uint64_t CommitEmptyImpl::GetGeneration() const {
  FTL_NOTIMPLEMENTED();
  return 0;
}

ObjectIdView CommitEmptyImpl::GetRootId() const {
  FTL_NOTIMPLEMENTED();
  return "NOT_IMPLEMENTED";
}

ftl::StringView CommitEmptyImpl::GetStorageBytes() const {
  FTL_NOTIMPLEMENTED();
  return "NOT_IMPLEMENTED";
}

}  // namespace test
}  // namespace storage
