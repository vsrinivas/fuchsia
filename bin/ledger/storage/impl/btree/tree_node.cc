// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/tree_node.h"

#include <algorithm>
#include <utility>

#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/impl/btree/encoding.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/socket/strings.h"

namespace storage {

TreeNode::TreeNode(PageStorage* page_storage,
                   std::string id,
                   uint8_t level,
                   std::vector<Entry> entries,
                   std::vector<ObjectId> children)
    : page_storage_(page_storage),
      id_(std::move(id)),
      level_(level),
      entries_(std::move(entries)),
      children_(std::move(children)) {
  FTL_DCHECK(entries_.size() + 1 == children_.size());
}

TreeNode::~TreeNode() {}

void TreeNode::FromId(
    PageStorage* page_storage,
    ObjectIdView id,
    std::function<void(Status, std::unique_ptr<const TreeNode>)> callback) {
  std::unique_ptr<const Object> object;
  page_storage->GetObject(id, PageStorage::Location::NETWORK, [
    page_storage, callback = std::move(callback)
  ](Status status, std::unique_ptr<const Object> object) {
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }
    std::unique_ptr<const TreeNode> node;
    status = FromObject(page_storage, std::move(object), &node);
    callback(status, std::move(node));
  });
}

void TreeNode::Empty(PageStorage* page_storage,
                     std::function<void(Status, ObjectId)> callback) {
  FromEntries(page_storage, 0u, std::vector<Entry>(), std::vector<ObjectId>(1),
              std::move(callback));
}

void TreeNode::FromEntries(PageStorage* page_storage,
                           uint8_t level,
                           const std::vector<Entry>& entries,
                           const std::vector<ObjectId>& children,
                           std::function<void(Status, ObjectId)> callback) {
  FTL_DCHECK(entries.size() + 1 == children.size());
  std::string encoding = storage::EncodeNode(level, entries, children);
  page_storage->AddObjectFromLocal(
      storage::DataSource::Create(std::move(encoding)), std::move(callback));
}

int TreeNode::GetKeyCount() const {
  return entries_.size();
}

Status TreeNode::GetEntry(int index, Entry* entry) const {
  FTL_DCHECK(index >= 0 && index < GetKeyCount());
  *entry = entries_[index];
  return Status::OK;
}

void TreeNode::GetChild(
    int index,
    std::function<void(Status, std::unique_ptr<const TreeNode>)> callback)
    const {
  FTL_DCHECK(index >= 0 && index <= GetKeyCount());
  if (children_[index].empty()) {
    callback(Status::NO_SUCH_CHILD, nullptr);
    return;
  }
  return FromId(page_storage_, children_[index], std::move(callback));
}

ObjectIdView TreeNode::GetChildId(int index) const {
  FTL_DCHECK(index >= 0 && index <= GetKeyCount());
  return children_[index];
}

Status TreeNode::FindKeyOrChild(convert::ExtendedStringView key,
                                int* index) const {
  if (key.empty()) {
    *index = 0;
    return !entries_.empty() && entries_[0].key.empty() ? Status::OK
                                                        : Status::NOT_FOUND;
  }
  auto it =
      std::lower_bound(entries_.begin(), entries_.end(), key,
                       [](const Entry& entry, convert::ExtendedStringView key) {
                         return entry.key < key;
                       });
  if (it == entries_.end()) {
    *index = entries_.size();
    return Status::NOT_FOUND;
  }
  *index = it - entries_.begin();
  if (it->key == key) {
    return Status::OK;
  }
  return Status::NOT_FOUND;
}

const ObjectId& TreeNode::GetId() const {
  return id_;
}

Status TreeNode::FromObject(PageStorage* page_storage,
                            std::unique_ptr<const Object> object,
                            std::unique_ptr<const TreeNode>* node) {
  ftl::StringView data;
  Status status = object->GetData(&data);
  if (status != Status::OK) {
    return status;
  }
  uint8_t level;
  std::vector<Entry> entries;
  std::vector<ObjectId> children;
  if (!DecodeNode(data, &level, &entries, &children)) {
    return Status::FORMAT_ERROR;
  }
  node->reset(new TreeNode(page_storage, object->GetId(), level,
                           std::move(entries), std::move(children)));
  return Status::OK;
}

}  // namespace storage
