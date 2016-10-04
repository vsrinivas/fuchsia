// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/btree/commit_contents_impl.h"

#include <memory>
#include <string>

#include "apps/ledger/storage/impl/btree/btree_iterator.h"
#include "apps/ledger/storage/public/commit_contents.h"
#include "lib/ftl/logging.h"

namespace storage {

CommitContentsImpl::CommitContentsImpl(const ObjectId& root_id,
                                       ObjectStore* store)
    : root_id_(root_id), store_(store) {}

CommitContentsImpl::~CommitContentsImpl() {}

std::unique_ptr<Iterator<const Entry>> CommitContentsImpl::begin() const {
  return std::unique_ptr<BTreeIterator>(NewIterator());
}

std::unique_ptr<Iterator<const Entry>> CommitContentsImpl::find(
    const std::string& key) const {
  std::unique_ptr<BTreeIterator> it(NewIterator());
  it->Seek(key);
  return std::unique_ptr<Iterator<const Entry>>(std::move(it));
}

std::unique_ptr<Iterator<const EntryChange>> CommitContentsImpl::diff(
    const CommitContents& other) const {
  FTL_NOTIMPLEMENTED();
  return nullptr;
}

std::unique_ptr<BTreeIterator> CommitContentsImpl::NewIterator() const {
  std::unique_ptr<const TreeNode> root;
  FTL_CHECK(store_->GetTreeNode(root_id_, &root) == Status::OK);
  return std::unique_ptr<BTreeIterator>(new BTreeIterator(std::move(root)));
}

}  // namespace storage
