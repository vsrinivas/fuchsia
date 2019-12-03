// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_db.h"

#include <lib/async/cpp/task.h>

#include "src/ledger/bin/storage/fake/fake_object.h"
#include "src/lib/fxl/strings/string_view.h"

namespace storage {
namespace fake {

namespace {
Status MakeEmptySyncCallAndCheck(async_dispatcher_t* dispatcher,
                                 coroutine::CoroutineHandler* handler) {
  if (coroutine::SyncCall(handler, [&dispatcher](fit::closure on_done) {
        async::PostTask(dispatcher, std::move(on_done));
      }) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  return Status::OK;
}

bool StartsWith(const std::string& key, convert::ExtendedStringView prefix) {
  return fxl::StringView(key.data(), std::min(key.size(), prefix.size())) == prefix;
}

class FakeBatch : public Db::Batch {
 public:
  FakeBatch(async_dispatcher_t* dispatcher, std::map<std::string, std::string>* key_value_store)
      : dispatcher_(dispatcher), key_value_store_(key_value_store) {}
  FakeBatch(const FakeBatch&) = delete;
  FakeBatch& operator=(const FakeBatch&) = delete;
  ~FakeBatch() override = default;

  Status Put(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
             fxl::StringView value) override {
    std::string key_str = key.ToString();
    entries_to_put_[key_str] = value.ToString();

    // Inserting an entry means that any previous |Delete| operations on that
    // key are cancelled: erase that key from |entries_to_delete_| if present.
    auto it = entries_to_delete_.find(key_str);
    if (it != entries_to_delete_.end()) {
      entries_to_delete_.erase(it);
    }
    return MakeEmptySyncCallAndCheck(dispatcher_, handler);
  }

  Status Delete(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) override {
    std::string key_str = key.ToString();
    entries_to_delete_.insert(key_str);

    // Deleting an entry means that any previous |Put| operations on that key
    // is cancelled: erase that entry from |entries_to_put_| if present.
    auto it = entries_to_put_.find(key_str);
    if (it != entries_to_put_.end()) {
      entries_to_put_.erase(it);
    }
    return MakeEmptySyncCallAndCheck(dispatcher_, handler);
  }

  Status Execute(coroutine::CoroutineHandler* handler) override {
    for (const auto& entry : entries_to_put_) {
      (*key_value_store_)[entry.first] = entry.second;
    }
    for (const auto& key : entries_to_delete_) {
      auto it = key_value_store_->find(key);
      if (it != key_value_store_->end()) {
        key_value_store_->erase(it);
      }
    }
    return MakeEmptySyncCallAndCheck(dispatcher_, handler);
  }

 private:
  async_dispatcher_t* dispatcher_;

  std::map<std::string, std::string> entries_to_put_;
  std::set<std::string> entries_to_delete_;

  std::map<std::string, std::string>* key_value_store_;
};

// A wrapper storage::Iterator for the elements of an std::map that start with a
// given prefix.
class PrefixIterator
    : public storage::Iterator<
          const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>> {
 public:
  PrefixIterator(const std::map<std::string, std::string>& key_value_store,
                 convert::ExtendedStringView prefix)
      : key_value_store_(key_value_store),
        prefix_(prefix.ToString()),
        it_(key_value_store_.lower_bound(prefix_)),
        end_(key_value_store_.end()) {
    UpdateCurrentElement();
  }

  PrefixIterator(const PrefixIterator&) = delete;
  PrefixIterator& operator=(const PrefixIterator&) = delete;
  ~PrefixIterator() override = default;

  storage::Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>&
  Next() override {
    ++it_;
    UpdateCurrentElement();
    return *this;
  }

  bool Valid() const override {
    return current_.has_value() &&
           current_.value().first.substr(0, prefix_.size()) == convert::ExtendedStringView(prefix_);
  }

  Status GetStatus() const override { return Status::OK; }

  const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>& operator*()
      const override {
    FXL_DCHECK(current_.has_value());
    return current_.value();
  }

  const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>* operator->()
      const override {
    FXL_DCHECK(current_.has_value());
    return &current_.value();
  }

 private:
  void UpdateCurrentElement() {
    if (it_ != end_) {
      current_ = {convert::ExtendedStringView(it_->first),
                  convert::ExtendedStringView(it_->second)};
    } else {
      current_.reset();
    }
  }

  // Snapshot of the database at the time of creation of the iterator.
  const std::map<std::string, std::string> key_value_store_;
  std::string prefix_;
  std::optional<std::pair<convert::ExtendedStringView, convert::ExtendedStringView>> current_;
  std::map<std::string, std::string>::const_iterator it_;
  std::map<std::string, std::string>::const_iterator end_;
};

}  // namespace

FakeDb::FakeDb(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}
FakeDb::~FakeDb() = default;

Status FakeDb::StartBatch(coroutine::CoroutineHandler* handler, std::unique_ptr<Batch>* batch) {
  *batch = std::make_unique<FakeBatch>(dispatcher_, &key_value_store_);
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::Get(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
                   std::string* value) {
  FXL_DCHECK(value);
  auto it = key_value_store_.find(key.ToString());
  if (it == key_value_store_.end()) {
    return Status::INTERNAL_NOT_FOUND;
  }
  *value = it->second;
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::HasKey(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) {
  auto it = key_value_store_.find(key.ToString());
  if (it == key_value_store_.end()) {
    return Status::INTERNAL_NOT_FOUND;
  }
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::HasPrefix(coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix) {
  auto it = key_value_store_.lower_bound(prefix.ToString());
  if (it == key_value_store_.end() || !StartsWith(it->first, prefix)) {
    return Status::INTERNAL_NOT_FOUND;
  }
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::GetObject(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
                         ObjectIdentifier object_identifier, std::unique_ptr<const Piece>* piece) {
  auto it = key_value_store_.find(key.ToString());
  if (it == key_value_store_.end()) {
    return Status::INTERNAL_NOT_FOUND;
  }
  if (piece) {
    *piece = std::make_unique<FakePiece>(std::move(object_identifier), it->second);
  }
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::GetByPrefix(coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
                           std::vector<std::string>* key_suffixes) {
  std::vector<std::string> keys_with_prefix;
  auto it = key_value_store_.lower_bound(prefix.ToString());
  while (it != key_value_store_.end() && StartsWith(it->first, prefix)) {
    keys_with_prefix.push_back(it->first.substr(prefix.size()));
    ++it;
  }
  key_suffixes->swap(keys_with_prefix);
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::GetEntriesByPrefix(coroutine::CoroutineHandler* handler,
                                  convert::ExtendedStringView prefix,
                                  std::vector<std::pair<std::string, std::string>>* entries) {
  std::vector<std::pair<std::string, std::string>> entries_with_prefix;
  auto it = key_value_store_.lower_bound(prefix.ToString());
  while (it != key_value_store_.end() && StartsWith(it->first, prefix)) {
    entries_with_prefix.emplace_back(it->first.substr(prefix.size()), it->second);
    ++it;
  }
  entries->swap(entries_with_prefix);
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::GetIteratorAtPrefix(
    coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
    std::unique_ptr<
        Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>*
        iterator) {
  *iterator = std::make_unique<PrefixIterator>(key_value_store_, prefix);
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

}  // namespace fake
}  // namespace storage
