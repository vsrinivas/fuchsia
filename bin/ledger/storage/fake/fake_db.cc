// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_db.h"

#include <lib/async/cpp/task.h>

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

bool HasPrefix(const std::string& key, convert::ExtendedStringView prefix) {
  return key.size() >= prefix.size() &&
         key.compare(0, prefix.size(), prefix.ToString());
}

class FakeBatch : public Db::Batch {
 public:
  FakeBatch(async_dispatcher_t* dispatcher,
            std::map<std::string, std::string>* key_value_store)
      : dispatcher_(dispatcher), key_value_store_(key_value_store) {}
  ~FakeBatch() override {}

  Status Put(coroutine::CoroutineHandler* handler,
             convert::ExtendedStringView key, fxl::StringView value) override {
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

  Status Delete(coroutine::CoroutineHandler* handler,
                convert::ExtendedStringView key) override {
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

  Status DeleteByPrefix(coroutine::CoroutineHandler* handler,
                        convert::ExtendedStringView prefix) override {
    for (auto it = key_value_store_->lower_bound(prefix.ToString());
         it != key_value_store_->end() && HasPrefix(it->first, prefix); ++it) {
      Delete(handler, it->first);
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

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeBatch);
};

}  // namespace

FakeDb::FakeDb(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}
FakeDb::~FakeDb() {}

Status FakeDb::StartBatch(coroutine::CoroutineHandler* handler,
                          std::unique_ptr<Batch>* batch) {
  *batch = std::make_unique<FakeBatch>(dispatcher_, &key_value_store_);
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::Get(coroutine::CoroutineHandler* handler,
                   convert::ExtendedStringView key, std::string* value) {
  auto it = key_value_store_.find(key.ToString());
  if (it == key_value_store_.end()) {
    return Status::NOT_FOUND;
  }
  *value = it->second;
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::HasKey(coroutine::CoroutineHandler* handler,
                      convert::ExtendedStringView key, bool* has_key) {
  auto it = key_value_store_.find(key.ToString());
  *has_key = it != key_value_store_.end();
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::GetObject(coroutine::CoroutineHandler* /*handler*/,
                         convert::ExtendedStringView /*key*/,
                         ObjectIdentifier /*object_identifier*/,
                         std::unique_ptr<const Object>* /*object*/) {
  FXL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

Status FakeDb::GetByPrefix(coroutine::CoroutineHandler* handler,
                           convert::ExtendedStringView prefix,
                           std::vector<std::string>* key_suffixes) {
  std::vector<std::string> keys_with_prefix;
  auto it = key_value_store_.lower_bound(prefix.ToString());
  while (it != key_value_store_.end() && HasPrefix(it->first, prefix)) {
    keys_with_prefix.push_back(it->first.substr(prefix.size()));
  }
  key_suffixes->swap(keys_with_prefix);
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::GetEntriesByPrefix(
    coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
    std::vector<std::pair<std::string, std::string>>* entries) {
  std::vector<std::pair<std::string, std::string>> entries_with_prefix;

  auto it = key_value_store_.lower_bound(prefix.ToString());
  while (it != key_value_store_.end() && HasPrefix(it->first, prefix)) {
    entries_with_prefix.emplace_back(it->first, it->second);
  }
  entries->swap(entries_with_prefix);
  return MakeEmptySyncCallAndCheck(dispatcher_, handler);
}

Status FakeDb::GetIteratorAtPrefix(
    coroutine::CoroutineHandler* /*handler*/,
    convert::ExtendedStringView /*prefix*/,
    std::unique_ptr<
        Iterator<const std::pair<convert::ExtendedStringView,
                                 convert::ExtendedStringView>>>* /*iterator*/) {
  FXL_NOTIMPLEMENTED();
  return Status::NOT_IMPLEMENTED;
}

}  // namespace fake
}  // namespace storage
