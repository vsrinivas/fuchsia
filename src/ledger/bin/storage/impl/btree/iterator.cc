// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/btree/iterator.h"

#include <lib/fit/function.h>

#include "src/ledger/bin/storage/impl/btree/internal_helper.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace btree {

namespace {

Status SynchronousForEachEntryInternal(SynchronousStorage* storage,
                                       LocatedObjectIdentifier root_identifier,
                                       std::optional<std::string> min_key,
                                       fit::function<bool(Entry)> on_next_entry,
                                       fit::function<bool(ObjectIdentifier)> on_next_node) {
  BTreeIterator iterator(storage);
  RETURN_ON_ERROR(iterator.Init(std::move(root_identifier)));
  if (min_key) {
    RETURN_ON_ERROR(iterator.SkipTo(*min_key));
  }
  while (!iterator.Finished()) {
    if (iterator.IsNewNode()) {
      if (!on_next_node(iterator.GetIdentifier())) {
        return Status::OK;
      }
    }
    if (iterator.HasValue()) {
      if (!on_next_entry(iterator.CurrentEntry())) {
        return Status::OK;
      }
    }
    RETURN_ON_ERROR(iterator.Advance());
  }
  return Status::OK;
}

void ForEachEntryInternal(coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
                          LocatedObjectIdentifier root_identifier,
                          std::optional<std::string> min_key,
                          fit::function<bool(Entry)> on_next_entry,
                          fit::function<bool(ObjectIdentifier)> on_next_node,
                          fit::function<void(Status)> on_done) {
  LEDGER_DCHECK(root_identifier.identifier.object_digest().IsValid());
  coroutine_service->StartCoroutine(
      [page_storage, root_identifier = std::move(root_identifier), min_key = std::move(min_key),
       on_next_entry = std::move(on_next_entry), on_next_node = std::move(on_next_node),
       on_done = std::move(on_done)](coroutine::CoroutineHandler* handler) mutable {
        SynchronousStorage storage(page_storage, handler);
        on_done(SynchronousForEachEntryInternal(&storage, std::move(root_identifier),
                                                std::move(min_key), std::move(on_next_entry),
                                                std::move(on_next_node)));
      });
}

}  // namespace

BTreeIterator::BTreeIterator(SynchronousStorage* storage) : storage_(storage) {}

BTreeIterator::BTreeIterator(BTreeIterator&& other) noexcept = default;

BTreeIterator& BTreeIterator::operator=(BTreeIterator&& other) noexcept = default;

Status BTreeIterator::Init(LocatedObjectIdentifier node_identifier) {
  location_ = std::move(node_identifier.location);
  return Descend(node_identifier.identifier);
}

Status BTreeIterator::SkipTo(absl::string_view min_key) {
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

bool BTreeIterator::SkipToIndex(absl::string_view key) {
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
  return !stack_.empty() && !descending_ && CurrentIndex() < CurrentNode().entries().size();
}

bool BTreeIterator::IsNewNode() const {
  return !stack_.empty() && descending_ && CurrentIndex() == 0;
}

bool BTreeIterator::Finished() const { return stack_.empty(); }

const Entry& BTreeIterator::CurrentEntry() const {
  LEDGER_DCHECK(HasValue());
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

const TreeNode& BTreeIterator::CurrentNode() const { return *stack_.back().first; }

Status BTreeIterator::Descend(const ObjectIdentifier& node_identifier) {
  LEDGER_DCHECK(descending_);
  std::unique_ptr<const TreeNode> node;
  RETURN_ON_ERROR(storage_->TreeNodeFromIdentifier({node_identifier, location_}, &node));
  stack_.emplace_back(std::move(node), 0);
  return Status::OK;
}

void GetObjectIdentifiers(coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
                          LocatedObjectIdentifier root_identifier,
                          fit::function<void(Status, std::set<ObjectIdentifier>)> callback) {
  LEDGER_DCHECK(root_identifier.identifier.object_digest().IsValid());
  auto object_digests = std::make_unique<std::set<ObjectIdentifier>>();
  object_digests->insert(root_identifier.identifier);

  auto on_next_entry = [object_digests = object_digests.get()](Entry e) {
    object_digests->insert(e.object_identifier);
    return true;
  };
  auto on_next_node = [object_digests = object_digests.get()](ObjectIdentifier node_identifier) {
    object_digests->insert(node_identifier);
    return true;
  };
  auto on_done = [object_digests = std::move(object_digests),
                  callback = std::move(callback)](Status status) {
    if (status != Status::OK) {
      callback(status, std::set<ObjectIdentifier>());
      return;
    }
    callback(status, std::move(*object_digests));
  };
  ForEachEntryInternal(coroutine_service, page_storage, root_identifier, std::nullopt,
                       std::move(on_next_entry), std::move(on_next_node), std::move(on_done));
}

void GetObjectsFromSync(coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
                        LocatedObjectIdentifier root_identifier,
                        fit::function<void(Status)> callback) {
  auto waiter =
      fxl::MakeRefCounted<callback::Waiter<Status, std::unique_ptr<const Object>>>(Status::OK);
  auto on_next = [page_storage, waiter](Entry e) {
    if (e.priority == KeyPriority::EAGER) {
      page_storage->GetObject(e.object_identifier, PageStorage::Location::ValueFromNetwork(),
                              waiter->NewCallback());
    }
    return true;
  };
  auto on_done = [callback = std::move(callback), waiter](Status status) mutable {
    if (status != Status::OK) {
      callback(status);
      return;
    }
    waiter->Finalize(
        [callback = std::move(callback)](
            Status s, std::vector<std::unique_ptr<const Object>> objects) { callback(s); });
  };
  ForEachEntryInternal(
      coroutine_service, page_storage, root_identifier, std::nullopt, std::move(on_next),
      [](ObjectIdentifier /*node*/) { return true; }, std::move(on_done));
}

void ForEachEntry(coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
                  LocatedObjectIdentifier root_identifier, std::string min_key,
                  fit::function<bool(Entry)> on_next, fit::function<void(Status)> on_done) {
  ForEachEntryInternal(
      coroutine_service, page_storage, std::move(root_identifier), std::move(min_key),
      std::move(on_next), [](ObjectIdentifier /*node*/) { return true; }, std::move(on_done));
}

}  // namespace btree
}  // namespace storage
