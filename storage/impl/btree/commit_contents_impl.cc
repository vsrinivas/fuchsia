// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/btree/commit_contents_impl.h"

#include <memory>
#include <string>

#include "apps/ledger/convert/convert.h"
#include "apps/ledger/storage/impl/btree/btree_iterator.h"
#include "apps/ledger/storage/impl/btree/diff_iterator.h"
#include "apps/ledger/storage/public/commit_contents.h"
#include "lib/ftl/logging.h"

namespace storage {

CommitContentsImpl::CommitContentsImpl(ObjectIdView root_id, ObjectStore* store)
    : root_id_(root_id.ToString()), store_(store) {}

CommitContentsImpl::~CommitContentsImpl() {}

std::unique_ptr<Iterator<const Entry>> CommitContentsImpl::begin() const {
  return std::unique_ptr<BTreeIterator>(NewIterator());
}

std::unique_ptr<Iterator<const Entry>> CommitContentsImpl::find(
    convert::ExtendedStringView key) const {
  std::unique_ptr<BTreeIterator> it(NewIterator());
  it->Seek(key);
  return std::unique_ptr<Iterator<const Entry>>(std::move(it));
}

std::unique_ptr<Iterator<const EntryChange>> CommitContentsImpl::diff(
    const CommitContents& other) const {
  std::unique_ptr<const TreeNode> root;
  // TODO(nellyv): Update API to return error Status. LE-39
  FTL_CHECK(TreeNode::FromId(store_, root_id_, &root) == Status::OK);

  std::unique_ptr<const TreeNode> right;
  FTL_CHECK(TreeNode::FromId(store_, other.GetBaseObjectId(), &right) ==
            Status::OK);

  return std::unique_ptr<DiffIterator>(
      new DiffIterator(std::move(root), std::move(right)));
}

ObjectId CommitContentsImpl::GetBaseObjectId() const {
  return root_id_;
}

std::unique_ptr<BTreeIterator> CommitContentsImpl::NewIterator() const {
  std::unique_ptr<const TreeNode> root;
  // TODO(nellyv): Update API to return error Status. LE-39
  FTL_CHECK(TreeNode::FromId(store_, root_id_, &root) == Status::OK);
  return std::unique_ptr<BTreeIterator>(new BTreeIterator(std::move(root)));
}

}  // namespace storage
