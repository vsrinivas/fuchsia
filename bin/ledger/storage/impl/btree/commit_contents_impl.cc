// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/commit_contents_impl.h"

#include <memory>
#include <string>

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/impl/btree/btree_iterator.h"
#include "apps/ledger/src/storage/impl/btree/diff_iterator.h"
#include "apps/ledger/src/storage/public/commit_contents.h"
#include "lib/ftl/logging.h"

namespace storage {

CommitContentsImpl::CommitContentsImpl(ObjectIdView root_id,
                                       PageStorage* page_storage)
    : root_id_(root_id.ToString()), page_storage_(page_storage) {}

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

void CommitContentsImpl::diff(
    const CommitContents& other,
    std::function<void(Status, std::unique_ptr<Iterator<const EntryChange>>)>
        callback) const {
  std::unique_ptr<const TreeNode> root;
  Status status = TreeNode::FromId(page_storage_, root_id_, &root);
  if (status != Status::OK) {
    callback(status, nullptr);
    return;
  }

  std::unique_ptr<const TreeNode> right;
  status = TreeNode::FromId(page_storage_, other.GetBaseObjectId(), &right);
  if (status != Status::OK) {
    callback(status, nullptr);
    return;
  }

  callback(Status::OK,
           std::make_unique<DiffIterator>(std::move(root), std::move(right)));
}

ObjectId CommitContentsImpl::GetBaseObjectId() const {
  return root_id_;
}

std::unique_ptr<BTreeIterator> CommitContentsImpl::NewIterator() const {
  std::unique_ptr<const TreeNode> root;
  // TODO(nellyv): Update API to return error Status. LE-39
  FTL_CHECK(TreeNode::FromId(page_storage_, root_id_, &root) == Status::OK);
  return std::make_unique<BTreeIterator>(std::move(root));
}

}  // namespace storage
