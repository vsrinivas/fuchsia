// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/db_view_factory.h"

#include <memory>
#include <string>

#include <src/lib/fxl/strings/concatenate.h>

#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/lib/convert/convert.h"

namespace ledger {
namespace {
class SubIterator : public storage::Iterator<
                        const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>> {
 public:
  SubIterator(std::unique_ptr<storage::Iterator<
                  const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
                  base_iterator,
              convert::ExtendedStringView prefix)
      : base_iterator_(std::move(base_iterator)), prefix_size_(prefix.length()) {
    UpdateCurrentValue();
  }

  ~SubIterator() override = default;

  storage::Iterator<const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>&
  Next() override {
    base_iterator_->Next();
    UpdateCurrentValue();
    return *this;
  }

  bool Valid() const override { return base_iterator_->Valid(); }

  Status GetStatus() const override { return base_iterator_->GetStatus(); }

  const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>& operator*()
      const override {
    return current_value_.value();
  }

  const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>* operator->()
      const override {
    return &(current_value_.value());
  }

 private:
  void UpdateCurrentValue() {
    current_value_.reset();
    if (base_iterator_->Valid()) {
      std::pair<convert::ExtendedStringView, convert::ExtendedStringView> value = **base_iterator_;
      value.first.remove_prefix(prefix_size_);
      current_value_.emplace(value);
    }
  }

  std::unique_ptr<storage::Iterator<
      const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>> const
      base_iterator_;
  const size_t prefix_size_;

  std::optional<std::pair<convert::ExtendedStringView, convert::ExtendedStringView>> current_value_;
};

class DbView : public storage::Db {
 public:
  class DbViewBatch : public storage::Db::Batch {
   public:
    explicit DbViewBatch(DbView* db_view, std::unique_ptr<storage::Db::Batch> batch)
        : db_view_(db_view), batch_(std::move(batch)){};
    ~DbViewBatch() override = default;

    Status Put(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
               fxl::StringView value) override {
      return batch_->Put(handler, fxl::Concatenate({db_view_->prefix_, key}), value);
    }

    Status Delete(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) override {
      return batch_->Delete(handler, fxl::Concatenate({db_view_->prefix_, key}));
    }

    Status Execute(coroutine::CoroutineHandler* handler) override {
      return batch_->Execute(handler);
    }

   private:
    DbView* const db_view_;
    std::unique_ptr<storage::Db::Batch> const batch_;
  };

  DbView(storage::Db* db, std::string prefix) : db_(db), prefix_(std::move(prefix)) {}
  ~DbView() override = default;

  Status StartBatch(coroutine::CoroutineHandler* handler, std::unique_ptr<Batch>* batch) override {
    std::unique_ptr<Batch> base_batch;
    auto status = db_->StartBatch(handler, &base_batch);
    *batch = std::make_unique<DbViewBatch>(this, std::move(base_batch));
    return status;
  }

  Status Get(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
             std::string* value) override {
    return db_->Get(handler, fxl::Concatenate({prefix_, key}), value);
  }

  Status HasKey(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key) override {
    return db_->HasKey(handler, fxl::Concatenate({prefix_, key}));
  }

  Status HasPrefix(coroutine::CoroutineHandler* handler,
                   convert::ExtendedStringView prefix) override {
    return db_->HasPrefix(handler, fxl::Concatenate({prefix_, prefix}));
  }

  // Retrieves the value for the given |key| as a Piece with the provided
  // |object_identifier|.
  Status GetObject(coroutine::CoroutineHandler* handler, convert::ExtendedStringView key,
                   storage::ObjectIdentifier object_identifier,
                   std::unique_ptr<const storage::Piece>* piece) override {
    return db_->GetObject(handler, fxl::Concatenate({prefix_, key}), std::move(object_identifier),
                          piece);
  }

  // Retrieves all keys matching the given |prefix|. |key_suffixes| will be
  // updated to contain the suffixes of corresponding keys.
  Status GetByPrefix(coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
                     std::vector<std::string>* key_suffixes) override {
    return db_->GetByPrefix(handler, fxl::Concatenate({prefix_, prefix}), key_suffixes);
  }

  // Retrieves all entries matching the given |prefix|. The keys of the
  // returned entries will be updated not to contain the |prefix|.
  Status GetEntriesByPrefix(coroutine::CoroutineHandler* handler,
                            convert::ExtendedStringView prefix,
                            std::vector<std::pair<std::string, std::string>>* entries) override {
    return db_->GetEntriesByPrefix(handler, fxl::Concatenate({prefix_, prefix}), entries);
  }

  // Retrieves an entry iterator over the entries whose keys start with
  // |prefix|.
  Status GetIteratorAtPrefix(
      coroutine::CoroutineHandler* handler, convert::ExtendedStringView prefix,
      std::unique_ptr<storage::Iterator<
          const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>* iterator)
      override {
    std::unique_ptr<storage::Iterator<
        const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>>
        base_iterator;
    RETURN_ON_ERROR(
        db_->GetIteratorAtPrefix(handler, fxl::Concatenate({prefix_, prefix}), &base_iterator));
    *iterator = std::make_unique<SubIterator>(std::move(base_iterator), prefix_);
    return Status::OK;
  }

 private:
  storage::Db* const db_;
  std::string const prefix_;
};

std::unique_ptr<storage::Db> CreateDbViewWithPrefix(storage::Db* db, std::string prefix) {
  return std::make_unique<DbView>(db, std::move(prefix));
}
}  // namespace

DbViewFactory::DbViewFactory(std::unique_ptr<storage::Db> db) : db_(std::move(db)) {}

DbViewFactory::~DbViewFactory() = default;

std::unique_ptr<storage::Db> DbViewFactory::CreateDbView(RepositoryRowPrefix prefix) {
  return CreateDbViewWithPrefix(db_.get(), ToString(prefix));
}

}  // namespace ledger
