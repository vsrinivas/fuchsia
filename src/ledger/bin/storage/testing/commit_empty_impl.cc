// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/testing/commit_empty_impl.h"

#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

std::unique_ptr<const Commit> CommitEmptyImpl::Clone() const {
  LEDGER_NOTIMPLEMENTED();
  return nullptr;
}

const CommitId& CommitEmptyImpl::GetId() const {
  static std::string id = "NOT_IMPLEMENTED";
  LEDGER_NOTIMPLEMENTED();
  return id;
}

std::vector<CommitIdView> CommitEmptyImpl::GetParentIds() const {
  LEDGER_NOTIMPLEMENTED();
  return {};
}

zx::time_utc CommitEmptyImpl::GetTimestamp() const {
  LEDGER_NOTIMPLEMENTED();
  return zx::time_utc();
}

uint64_t CommitEmptyImpl::GetGeneration() const {
  LEDGER_NOTIMPLEMENTED();
  return 0;
}

ObjectIdentifier CommitEmptyImpl::GetRootIdentifier() const {
  LEDGER_NOTIMPLEMENTED();
  return ObjectIdentifier();
}

absl::string_view CommitEmptyImpl::GetStorageBytes() const {
  LEDGER_NOTIMPLEMENTED();
  return "NOT_IMPLEMENTED";
}

bool CommitEmptyImpl::IsAlive() const {
  LEDGER_NOTIMPLEMENTED();
  return true;
}

}  // namespace storage
