// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/iterator.h"

#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/callback/waiter.h"
#include "peridot/bin/ledger/storage/impl/btree/internal_helper.h"

namespace storage {
namespace btree {

namespace {

Status ForEachEntryInternal(
    SynchronousStorage* storage,
    ObjectDigestView root_digest,
    fxl::StringView min_key,
    const std::function<bool(EntryAndNodeDigest)>& on_next) {
  BTreeIterator iterator(storage);
  RETURN_ON_ERROR(iterator.Init(root_digest));
  RETURN_ON_ERROR(iterator.SkipTo(min_key));
  while (!iterator.Finished()) {
    RETURN_ON_ERROR(iterator.AdvanceToValue());
    if (iterator.HasValue()) {
      if (!on_next({iterator.CurrentEntry(), iterator.GetDigest()})) {
        return Status::OK;
      }
      RETURN_ON_ERROR(iterator.Advance());
    }
  }
  return Status::OK;
}

}  // namespace

BTreeIterator::BTreeIterator(SynchronousStorage* storage) : storage_(storage) {}

BTreeIterator::BTreeIterator(BTreeIterator&& other) = default;

BTreeIterator& BTreeIterator::operator=(BTreeIterator&& other) = default;

Status BTreeIterator::Init(ObjectDigestView node_digest) {
  return Descend(node_digest);
}

Status BTreeIterator::SkipTo(fxl::StringView min_key) {
  descending_ = true;
  for (;;) {
    if (SkipToIndex(min_key)) {
      return Status::OK;
    }
    auto next_child = GetNextChild();
    if (next_child.empty()) {
      return Status::OK;
    }
    RETURN_ON_ERROR(Descend(next_child));
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

fxl::StringView BTreeIterator::GetNextChild() const {
  auto index = CurrentIndex();
  auto& children_digests = CurrentNode().children_digests();
  if (descending_) {
    return children_digests[index];
  }
  if (index + 1 < children_digests.size()) {
    return children_digests[index + 1];
  }
  return "";
}

bool BTreeIterator::HasValue() const {
  return !stack_.empty() && !descending_ &&
         CurrentIndex() < CurrentNode().entries().size();
}

bool BTreeIterator::Finished() const {
  return stack_.empty();
}

const Entry& BTreeIterator::CurrentEntry() const {
  FXL_DCHECK(HasValue());
  return CurrentNode().entries()[CurrentIndex()];
}

const ObjectDigest& BTreeIterator::GetDigest() const {
  return CurrentNode().GetDigest();
}

uint8_t BTreeIterator::GetLevel() const {
  return CurrentNode().level();
}

Status BTreeIterator::Advance() {
  if (descending_) {
    return Descend(GetNextChild());
  }

  auto& index = CurrentIndex();
  ++index;
  if (index < CurrentNode().children_digests().size()) {
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

size_t& BTreeIterator::CurrentIndex() {
  return stack_.back().second;
}

size_t BTreeIterator::CurrentIndex() const {
  return stack_.back().second;
}

const TreeNode& BTreeIterator::CurrentNode() const {
  return *stack_.back().first;
}

Status BTreeIterator::Descend(fxl::StringView node_digest) {
  FXL_DCHECK(descending_);
  if (node_digest.empty()) {
    descending_ = false;
    return Status::OK;
  }

  std::unique_ptr<const TreeNode> node;
  RETURN_ON_ERROR(storage_->TreeNodeFromDigest(node_digest, &node));
  stack_.emplace_back(std::move(node), 0);
  return Status::OK;
}

void GetObjectDigests(
    coroutine::CoroutineService* coroutine_service,
    PageStorage* page_storage,
    ObjectDigestView root_digest,
    std::function<void(Status, std::set<ObjectDigest>)> callback) {
  FXL_DCHECK(!root_digest.empty());
  auto object_digests = std::make_unique<std::set<ObjectDigest>>();
  object_digests->insert(root_digest.ToString());

  auto on_next = [object_digests = object_digests.get()](EntryAndNodeDigest e) {
    object_digests->insert(e.entry.object_digest);
    object_digests->insert(e.node_digest);
    return true;
  };
  auto on_done = fxl::MakeCopyable([
    object_digests = std::move(object_digests), callback = std::move(callback)
  ](Status status) {
    if (status != Status::OK) {
      callback(status, std::set<ObjectDigest>());
      return;
    }
    callback(status, std::move(*object_digests));
  });
  ForEachEntry(coroutine_service, page_storage, root_digest, "",
               std::move(on_next), std::move(on_done));
}

void GetObjectsFromSync(coroutine::CoroutineService* coroutine_service,
                        PageStorage* page_storage,
                        ObjectDigestView root_digest,
                        std::function<void(Status)> callback) {
  fxl::RefPtr<callback::Waiter<Status, std::unique_ptr<const Object>>> waiter_ =
      callback::Waiter<Status, std::unique_ptr<const Object>>::Create(
          Status::OK);
  auto on_next = [page_storage, waiter_](EntryAndNodeDigest e) {
    if (e.entry.priority == KeyPriority::EAGER) {
      page_storage->GetObject(e.entry.object_digest,
                              PageStorage::Location::NETWORK,
                              waiter_->NewCallback());
    }
    return true;
  };
  auto on_done =
      [ callback = std::move(callback), waiter_ ](Status status) mutable {
    if (status != Status::OK) {
      callback(status);
      return;
    }
    waiter_->Finalize([callback = std::move(callback)](
        Status s, std::vector<std::unique_ptr<const Object>> objects) {
      callback(s);
    });
  };
  ForEachEntry(coroutine_service, page_storage, root_digest, "",
               std::move(on_next), std::move(on_done));
}

void ForEachEntry(coroutine::CoroutineService* coroutine_service,
                  PageStorage* page_storage,
                  ObjectDigestView root_digest,
                  std::string min_key,
                  std::function<bool(EntryAndNodeDigest)> on_next,
                  std::function<void(Status)> on_done) {
  FXL_DCHECK(!root_digest.empty());
  coroutine_service->StartCoroutine([
    page_storage, root_digest, min_key = std::move(min_key),
    on_next = std::move(on_next), on_done = std::move(on_done)
  ](coroutine::CoroutineHandler * handler) {
    SynchronousStorage storage(page_storage, handler);

    on_done(ForEachEntryInternal(&storage, root_digest, min_key, on_next));
  });
}

}  // namespace btree
}  // namespace storage
