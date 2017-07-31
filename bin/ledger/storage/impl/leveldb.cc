// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/leveldb.h"

#include "apps/ledger/src/storage/impl/object_impl.h"
#include "lib/ftl/files/directory.h"

namespace storage {

namespace {

Status ConvertStatus(leveldb::Status s) {
  if (s.IsNotFound()) {
    return Status::NOT_FOUND;
  }
  if (!s.ok()) {
    FTL_LOG(ERROR) << "LevelDB error: " << s.ToString();
    return Status::INTERNAL_IO_ERROR;
  }
  return Status::OK;
}

class BatchImpl : public Db::Batch {
 public:
  explicit BatchImpl(std::function<Status(bool)> callback)
      : callback_(callback), executed_(false) {}

  ~BatchImpl() override {
    if (!executed_)
      callback_(false);
  }

  Status Execute() override {
    FTL_DCHECK(!executed_);
    executed_ = true;
    return callback_(true);
  }

 private:
  std::function<Status(bool)> callback_;
  bool executed_;
};

class RowIterator
    : public Iterator<const std::pair<convert::ExtendedStringView,
                                      convert::ExtendedStringView>> {
 public:
  RowIterator(std::unique_ptr<leveldb::Iterator> it, std::string prefix)
      : it_(std::move(it)), prefix_(std::move(prefix)) {
    PrepareEntry();
  }

  ~RowIterator() override {}

  Iterator<const std::pair<convert::ExtendedStringView,
                           convert::ExtendedStringView>>&
  Next() override {
    it_->Next();
    PrepareEntry();
    return *this;
  }

  bool Valid() const override {
    return it_->Valid() && it_->key().starts_with(prefix_);
  }

  Status GetStatus() const override {
    return it_->status().ok() ? Status::OK : Status::INTERNAL_IO_ERROR;
  }

  const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>&
  operator*() const override {
    return *(row_.get());
  }

  const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>*
  operator->() const override {
    return row_.get();
  }

 private:
  void PrepareEntry() {
    if (!Valid()) {
      row_.reset(nullptr);
      return;
    }
    row_ = std::make_unique<
        std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>(
        it_->key(), it_->value());
  }

  std::unique_ptr<leveldb::Iterator> it_;
  const std::string prefix_;

  std::unique_ptr<
      std::pair<convert::ExtendedStringView, convert::ExtendedStringView>>
      row_;
};

}  // namespace

LevelDb::LevelDb(std::string db_path) : db_path_(std::move(db_path)) {}

LevelDb::~LevelDb() {
  FTL_DCHECK(!batch_);
}

Status LevelDb::Init() {
  if (!files::CreateDirectory(db_path_)) {
    FTL_LOG(ERROR) << "Failed to create directory under " << db_path_;
    return Status::INTERNAL_IO_ERROR;
  }
  leveldb::DB* db = nullptr;
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::Status status = leveldb::DB::Open(options, db_path_, &db);
  if (!status.ok()) {
    FTL_LOG(ERROR) << "Failed to open ledger at " << db_path_
                   << " with status: " << status.ToString();
    return Status::INTERNAL_IO_ERROR;
  }
  db_.reset(db);
  return Status::OK;
}

std::unique_ptr<Db::Batch> LevelDb::StartBatch() {
  FTL_DCHECK(!batch_);
  batch_ = std::make_unique<leveldb::WriteBatch>();
  return std::make_unique<BatchImpl>([this](bool execute) {
    std::unique_ptr<leveldb::WriteBatch> batch = std::move(batch_);
    if (execute) {
      leveldb::Status status = db_->Write(write_options_, batch.get());
      if (!status.ok()) {
        FTL_LOG(ERROR) << "Failed to execute batch with status: "
                       << status.ToString();
        return Status::INTERNAL_IO_ERROR;
      }
    }
    return Status::OK;
  });
}

bool LevelDb::BatchStarted() {
  return batch_ != nullptr;
}

Status LevelDb::Put(convert::ExtendedStringView key, ftl::StringView value) {
  if (batch_) {
    batch_->Put(key, convert::ToSlice(value));
    return Status::OK;
  }
  return ConvertStatus(db_->Put(write_options_, key, convert::ToSlice(value)));
}

Status LevelDb::Get(convert::ExtendedStringView key, std::string* value) {
  return ConvertStatus(db_->Get(read_options_, key, value));
}

Status LevelDb::Delete(convert::ExtendedStringView key) {
  if (batch_) {
    batch_->Delete(key);
    return Status::OK;
  }
  return ConvertStatus(db_->Delete(write_options_, key));
}

Status LevelDb::HasKey(convert::ExtendedStringView key, bool* has_key) {
  std::unique_ptr<leveldb::Iterator> iterator(db_->NewIterator(read_options_));
  iterator->Seek(key);

  *has_key = iterator->Valid() && iterator->key() == key;
  return Status::OK;
}

Status LevelDb::GetObject(convert::ExtendedStringView key,
                          ObjectId object_id,
                          std::unique_ptr<const Object>* object) {
  std::unique_ptr<leveldb::Iterator> iterator(db_->NewIterator(read_options_));
  iterator->Seek(key);

  if (!iterator->Valid() || iterator->key() != key) {
    return Status::NOT_FOUND;
  }

  if (object) {
    *object = std::make_unique<LevelDBObject>(std::move(object_id),
                                              std::move(iterator));
  }
  return Status::OK;
}

Status LevelDb::GetByPrefix(convert::ExtendedStringView prefix,
                            std::vector<std::string>* key_suffixes) {
  std::vector<std::string> result;
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix);
       it->Next()) {
    leveldb::Slice key = it->key();
    key.remove_prefix(prefix.size());
    result.push_back(key.ToString());
  }
  if (!it->status().ok()) {
    return ConvertStatus(it->status());
  }
  key_suffixes->swap(result);
  return Status::OK;
}

Status LevelDb::GetEntriesByPrefix(
    convert::ExtendedStringView prefix,
    std::vector<std::pair<std::string, std::string>>* entries) {
  std::vector<std::pair<std::string, std::string>> result;
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix);
       it->Next()) {
    leveldb::Slice key = it->key();
    key.remove_prefix(prefix.size());
    result.push_back(std::pair<std::string, std::string>(
        key.ToString(), it->value().ToString()));
  }
  if (!it->status().ok()) {
    return ConvertStatus(it->status());
  }
  entries->swap(result);
  return Status::OK;
}

Status LevelDb::GetIteratorAtPrefix(
    convert::ExtendedStringView prefix,
    std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                             convert::ExtendedStringView>>>*
        iterator) {
  std::unique_ptr<leveldb::Iterator> local_iterator(
      db_->NewIterator(read_options_));
  local_iterator->Seek(prefix);

  if (iterator) {
    std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                             convert::ExtendedStringView>>>
        row_iterator = std::make_unique<RowIterator>(std::move(local_iterator),
                                                     prefix.ToString());
    iterator->swap(row_iterator);
  }
  return Status::OK;
}

Status LevelDb::DeleteByPrefix(convert::ExtendedStringView prefix) {
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix);
       it->Next()) {
    Delete(it->key());
  }
  return ConvertStatus(it->status());
}

}  // namespace storage
