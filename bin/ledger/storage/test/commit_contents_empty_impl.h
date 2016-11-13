// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_CONTENTS_EMPTY_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_CONTENTS_EMPTY_IMPL_H_

#include <memory>
#include <vector>

#include "apps/ledger/src/storage/public/commit_contents.h"

namespace storage {
namespace test {

// Empty implementaton of CommitContents. All methods do nothing and return
// dummy or empty responses.
class CommitContentsEmptyImpl : public CommitContents {
 public:
  CommitContentsEmptyImpl() = default;
  ~CommitContentsEmptyImpl() override = default;

  // CommitContents:
  // Returns an iterator at the beginning of the contents.
  std::unique_ptr<Iterator<const Entry>> begin() const override;

  // Returns an iterator pointing to |key| if present, or pointing to the first
  // entry after |key| if |key| is not present.
  std::unique_ptr<Iterator<const Entry>> find(
      convert::ExtendedStringView key) const override;

  // Returns an iterator over the difference between this object and other
  // object.
  void diff(
      std::unique_ptr<CommitContents> other,
      std::function<void(Status, std::unique_ptr<Iterator<const EntryChange>>)>
          callback) const override;

  // Returns the id of the root node.
  ObjectId GetBaseObjectId() const override;
};

}  // namespace test
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_TEST_COMMIT_CONTENTS_EMPTY_IMPL_H_
