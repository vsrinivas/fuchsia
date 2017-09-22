// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/synchronous_storage.h"

namespace storage {
namespace btree {

SynchronousStorage::SynchronousStorage(PageStorage* page_storage,
                                       coroutine::CoroutineHandler* handler)
    : page_storage_(page_storage), handler_(handler) {}

Status SynchronousStorage::TreeNodeFromId(
    ObjectIdView object_id,
    std::unique_ptr<const TreeNode>* result) {
  Status status;
  if (coroutine::SyncCall(
          handler_,
          [this, &object_id](
              std::function<void(Status, std::unique_ptr<const TreeNode>)>
                  callback) {
            TreeNode::FromId(page_storage_, object_id, std::move(callback));
          },
          &status, result)) {
    return Status::INTERRUPTED;
  }
  return status;
}

Status SynchronousStorage::TreeNodesFromIds(
    std::vector<ObjectIdView> object_ids,
    std::vector<std::unique_ptr<const TreeNode>>* result) {
  auto waiter =
      callback::Waiter<Status, std::unique_ptr<const TreeNode>>::Create(
          Status::OK);
  for (const auto& object_id : object_ids) {
    TreeNode::FromId(page_storage_, object_id, waiter->NewCallback());
  }
  Status status;
  if (coroutine::SyncCall(
          handler_,
          [waiter](std::function<void(
                       Status, std::vector<std::unique_ptr<const TreeNode>>)>
                       callback) { waiter->Finalize(std::move(callback)); },
          &status, result)) {
    return Status::INTERRUPTED;
  }
  return status;
}

Status SynchronousStorage::TreeNodeFromEntries(
    uint8_t level,
    const std::vector<Entry>& entries,
    const std::vector<ObjectId>& children,
    ObjectId* result) {
  Status status;
  if (coroutine::SyncCall(handler_,
                          [this, level, &entries, &children](
                              std::function<void(Status, ObjectId)> callback) {
                            TreeNode::FromEntries(page_storage_, level, entries,
                                                  children,
                                                  std::move(callback));
                          },
                          &status, result)) {
    return Status::INTERRUPTED;
  }
  return status;
}

}  // namespace btree
}  // namespace storage
