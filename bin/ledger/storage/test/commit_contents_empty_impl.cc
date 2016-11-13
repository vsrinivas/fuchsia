// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/test/commit_contents_empty_impl.h"

#include "lib/ftl/logging.h"

namespace storage {
namespace test {

std::unique_ptr<Iterator<const Entry>> CommitContentsEmptyImpl::begin() const {
  FTL_NOTIMPLEMENTED();
  return nullptr;
}

// Returns an iterator pointing to |key| if present, or pointing to the first
// entry after |key| if |key| is not present.
std::unique_ptr<Iterator<const Entry>> CommitContentsEmptyImpl::find(
    convert::ExtendedStringView key) const {
  FTL_NOTIMPLEMENTED();
  return nullptr;
}

// Returns an iterator over the difference between this object and other
// object.
void CommitContentsEmptyImpl::diff(
    std::unique_ptr<CommitContents> other,
    std::function<void(Status, std::unique_ptr<Iterator<const EntryChange>>)>
        callback) const {
  FTL_NOTIMPLEMENTED();
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

// Returns the id of the root node.
ObjectId CommitContentsEmptyImpl::GetBaseObjectId() const {
  FTL_NOTIMPLEMENTED();
  return ObjectId();
}

}  // namespace test
}  // namespace storage
