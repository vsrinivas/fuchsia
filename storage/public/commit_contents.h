// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_COMMIT_CONTENTS_H_
#define APPS_LEDGER_STORAGE_PUBLIC_COMMIT_CONTENTS_H_

#include <iterator>
#include <memory>

#include "apps/ledger/convert/convert.h"
#include "apps/ledger/storage/public/iterator.h"
#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {

// The contents of a commit, independent of its on-disk representation.
class CommitContents {
 public:
  CommitContents() {}
  virtual ~CommitContents() {}

  // Returns an iterator at the beginning of the contents.
  virtual std::unique_ptr<Iterator<const Entry>> begin() const = 0;

  // Returns an iterator pointing to |key| if present, or pointing to the first
  // entry after |key| if |key| is not present.
  virtual std::unique_ptr<Iterator<const Entry>> find(
      convert::ExtendedStringView key) const = 0;

  // Returns an iterator over the difference between this object and other
  // object.
  virtual std::unique_ptr<Iterator<const EntryChange>> diff(
      const CommitContents& other) const = 0;

  // Returns the id of the root node.
  virtual ObjectId GetBaseObjectId() const = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(CommitContents);
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_PUBLIC_COMMIT_CONTENTS_H_
