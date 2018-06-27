// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/iterator.h"

#include <lib/fit/function.h>

#include "lib/callback/waiter.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/storage/impl/btree/internal_helper.h"

namespace storage {
namespace btree {

namespace {

Status ForEachEntryInternal(
    SynchronousStorage* storage, ObjectIdentifier root_identifier,
    fxl::StringView min_key,
    fit::function<bool(EntryAndNodeIdentifier)> on_next) {
  BTreeIterator iterator(storage);
  RETURN_ON_ERROR(iterator.Init(root_identifier));
  RETURN_ON_ERROR(iterator.SkipTo(min_key));
  while (!iterator.Finished()) {
    RETURN_ON_ERROR(iterator.AdvanceToValue());
    if (iterator.HasValue()) {
      if (!on_next({iterator.CurrentEntry(), iterator.GetIdentifier()})) {
        return Status::OK;
      }
      RETURN_ON_ERROR(iterator.Advance());
    }
  }
  return Status::OK;
}

}  // namespace

BTreeIterator::BTreeIterator(SynchronousStorage* storage) : storage_(storage) {}

BTreeIterator::BTreeIterator(BTreeIterator&& other) noexcept = default;

BTreeIterator& BTreeIterator::operator=(BTreeIterator&& other) noexcept =
    default;

Status BTreeIterator::Init(ObjectIdentifier node_identifier) {
  return Descend(node_identifier);
}

Status BTreeIterator::SkipTo(fxl::StringView min_key) {
  descending_ = true;
  for (;;) {
    if (SkipToIndex(min_key)) {
      return Status::OK;
    }
    auto next_child = GetNextChild();
    if (!next_child) {
      return Status::OK;
    }
    RETURN_ON_ERROR(Descend(*next_child));
  }
}

bool BTreeIterator::SkipToIndex(fxl::StringView key) {
  auto& entries = CurrentNode().entries();
  size_t skip_count = GetEntryOrChildIndex(entries, key);
  if (skip_count < CurrentIndex()) {
    return true;
  }
  CurrentIndex() = skip_count;
  if (CurrentIndex() < entries.size() && entries[CurrentIndex()].key == key) {
    descending_ = false;
    return true;
  }
  return false;
}

const ObjectIdentifier* BTreeIterator::GetNextChild() const {
  auto index = CurrentIndex();
  auto& children_identifiers = CurrentNode().children_identifiers();
  auto children_index = descending_ ? index : index + 1;
  auto it = children_identifiers.find(children_index);
  return it == children_identifiers.end() ? nullptr : &(it->second);
}

bool BTreeIterator::HasValue() const {
  return !stack_.empty() && !descending_ &&
         CurrentIndex() < CurrentNode().entries().size();
}

bool BTreeIterator::Finished() const { return stack_.empty(); }

const Entry& BTreeIterator::CurrentEntry() const {
  FXL_DCHECK(HasValue());
  return CurrentNode().entries()[CurrentIndex()];
}

const ObjectIdentifier& BTreeIterator::GetIdentifier() const {
  return CurrentNode().GetIdentifier();
}

uint8_t BTreeIterator::GetLevel() const { return CurrentNode().level(); }

Status BTreeIterator::Advance() {
  if (descending_) {
    auto child = GetNextChild();
    if (!child) {
      descending_ = false;
      return Status::OK;
    }
    return Descend(*child);
  }

  auto& index = CurrentIndex();
  ++index;
  if (index <= CurrentNode().entries().size()) {
    descending_ = true;
  } else {
    stack_.pop_back();
  }

  return Status::OK;
}

Status BTreeIterator::AdvanceToValue() {
  while (!Finished() && !HasValue()) {
    RETURN_ON_ERROR(Advance());
  }
  return Status::OK;
}

void BTreeIterator::SkipNextSubTree() {
  if (descending_) {
    descending_ = false;
  } else {
    ++CurrentIndex();
  }
}

size_t& BTreeIterator::CurrentIndex() { return stack_.back().second; }

size_t BTreeIterator::CurrentIndex() const { return stack_.back().second; }

const TreeNode& BTreeIterator::CurrentNode() const {
  return *stack_.back().first;
}

Status BTreeIterator::Descend(const ObjectIdentifier& node_identifier) {
  FXL_DCHECK(descending_);
  std::unique_ptr<const TreeNode> node;
  RETURN_ON_ERROR(storage_->TreeNodeFromIdentifier(node_identifier, &node));
  stack_.emplace_back(std::move(node), 0);
  return Status::OK;
}

void GetObjectIdentifiers(
    coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
    ObjectIdentifier root_identifier,
    fit::function<void(Status, std::set<ObjectIdentifier>)> callback) {
  FXL_DCHECK(!root_identifier.object_digest.empty());
  auto object_digests = std::make_unique<std::set<ObjectIdentifier>>();
  object_digests->insert(root_identifier);

  auto on_next = [object_digests =
                      object_digests.get()](EntryAndNodeIdentifier e) {
    object_digests->insert(e.entry.object_identifier);
    object_digests->insert(e.node_identifier);
    return true;
  };
  auto on_done =
      fxl::MakeCopyable([object_digests = std::move(object_digests),
                         callback = std::move(callback)](Status status) {
        if (status != Status::OK) {
          callback(status, std::set<ObjectIdentifier>());
          return;
        }
        callback(status, std::move(*object_digests));
      });
  ForEachEntry(coroutine_service, page_storage, root_identifier, "",
               std::move(on_next), std::move(on_done));
}

void GetObjectsFromSync(coroutine::CoroutineService* coroutine_service,
                        PageStorage* page_storage,
                        ObjectIdentifier root_identifier,
                        fit::function<void(Status)> callback) {
  auto waiter = fxl::MakeRefCounted<
      callback::Waiter<Status, std::unique_ptr<const Object>>>(Status::OK);
  auto on_next = [page_storage, waiter](EntryAndNodeIdentifier e) {
    if (e.entry.priority == KeyPriority::EAGER) {
      page_storage->GetObject(e.entry.object_identifier,
                              PageStorage::Location::NETWORK,
                              waiter->NewCallback());
    }
    return true;
  };
  auto on_done = [callback = std::move(callback),
                  waiter](Status status) mutable {
    if (status != Status::OK) {
      callback(status);
      return;
    }
    waiter->Finalize(
        [callback = std::move(callback)](
            Status s, std::vector<std::unique_ptr<const Object>> objects) {
          callback(s);
        });
  };
  ForEachEntry(coroutine_service, page_storage, root_identifier, "",
               std::move(on_next), std::move(on_done));
}

void ForEachEntry(coroutine::CoroutineService* coroutine_service,
                  PageStorage* page_storage, ObjectIdentifier root_identifier,
                  std::string min_key,
                  fit::function<bool(EntryAndNodeIdentifier)> on_next,
                  fit::function<void(Status)> on_done) {
  FXL_DCHECK(!root_identifier.object_digest.empty());
  coroutine_service->StartCoroutine(
      [page_storage, root_identifier = std::move(root_identifier),
       min_key = std::move(min_key), on_next = std::move(on_next),
       on_done =
           std::move(on_done)](coroutine::CoroutineHandler* handler) mutable {
        SynchronousStorage storage(page_storage, handler);

        on_done(ForEachEntryInternal(&storage, root_identifier, min_key,
                                     std::move(on_next)));
      });
}

}  // namespace btree
}  // namespace storage
