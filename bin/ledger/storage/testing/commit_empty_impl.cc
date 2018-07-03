// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/testing/commit_empty_impl.h"

#include <lib/fxl/logging.h>

namespace storage {

std::unique_ptr<Commit> CommitEmptyImpl::Clone() const {
  FXL_NOTIMPLEMENTED();
  return nullptr;
}

const CommitId& CommitEmptyImpl::GetId() const {
  static std::string id = "NOT_IMPLEMENTED";
  FXL_NOTIMPLEMENTED();
  return id;
}

std::vector<CommitIdView> CommitEmptyImpl::GetParentIds() const {
  FXL_NOTIMPLEMENTED();
  return {};
}

int64_t CommitEmptyImpl::GetTimestamp() const {
  FXL_NOTIMPLEMENTED();
  return 0;
}

uint64_t CommitEmptyImpl::GetGeneration() const {
  FXL_NOTIMPLEMENTED();
  return 0;
}

ObjectIdentifier CommitEmptyImpl::GetRootIdentifier() const {
  FXL_NOTIMPLEMENTED();
  return {0u, 0u, "NOT_IMPLEMENTED"};
}

fxl::StringView CommitEmptyImpl::GetStorageBytes() const {
  FXL_NOTIMPLEMENTED();
  return "NOT_IMPLEMENTED";
}

}  // namespace storage
