// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/testing/commit_empty_impl.h"

#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

std::unique_ptr<const Commit> CommitEmptyImpl::Clone() const {
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

zx::time_utc CommitEmptyImpl::GetTimestamp() const {
  FXL_NOTIMPLEMENTED();
  return zx::time_utc();
}

uint64_t CommitEmptyImpl::GetGeneration() const {
  FXL_NOTIMPLEMENTED();
  return 0;
}

ObjectIdentifier CommitEmptyImpl::GetRootIdentifier() const {
  FXL_NOTIMPLEMENTED();
  return ObjectIdentifier();
}

absl::string_view CommitEmptyImpl::GetStorageBytes() const {
  FXL_NOTIMPLEMENTED();
  return "NOT_IMPLEMENTED";
}

bool CommitEmptyImpl::IsAlive() const {
  FXL_NOTIMPLEMENTED();
  return true;
}

}  // namespace storage
