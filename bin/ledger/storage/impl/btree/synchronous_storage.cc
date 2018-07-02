// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/synchronous_storage.h"

#include <lib/fit/function.h>

#include "lib/fxl/memory/ref_ptr.h"

namespace storage {
namespace btree {

SynchronousStorage::SynchronousStorage(PageStorage* page_storage,
                                       coroutine::CoroutineHandler* handler)
    : page_storage_(page_storage), handler_(handler) {}

Status SynchronousStorage::TreeNodeFromIdentifier(
    ObjectIdentifier object_identifier,
    std::unique_ptr<const TreeNode>* result) {
  Status status;
  if (coroutine::SyncCall(
          handler_,
          [this, &object_identifier](
              fit::function<void(Status, std::unique_ptr<const TreeNode>)>
                  callback) {
            TreeNode::FromIdentifier(page_storage_, object_identifier,
                                     std::move(callback));
          },
          &status, result) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  return status;
}

Status SynchronousStorage::TreeNodesFromIdentifiers(
    std::vector<ObjectIdentifier> object_identifiers,
    std::vector<std::unique_ptr<const TreeNode>>* result) {
  auto waiter = fxl::MakeRefCounted<
      callback::Waiter<Status, std::unique_ptr<const TreeNode>>>(Status::OK);
  for (const auto& object_identifier : object_identifiers) {
    TreeNode::FromIdentifier(page_storage_, object_identifier,
                             waiter->NewCallback());
  }
  Status status;
  if (coroutine::SyncCall(
          handler_,
          [waiter](fit::function<void(
                       Status, std::vector<std::unique_ptr<const TreeNode>>)>
                       callback) { waiter->Finalize(std::move(callback)); },
          &status, result) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  return status;
}

Status SynchronousStorage::TreeNodeFromEntries(
    uint8_t level, const std::vector<Entry>& entries,
    const std::map<size_t, ObjectIdentifier>& children,
    ObjectIdentifier* result) {
  Status status;
  if (coroutine::SyncCall(
          handler_,
          [this, level, &entries,
           &children](fit::function<void(Status, ObjectIdentifier)> callback) {
            TreeNode::FromEntries(page_storage_, level, entries, children,
                                  std::move(callback));
          },
          &status, result) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  return status;
}

}  // namespace btree
}  // namespace storage
