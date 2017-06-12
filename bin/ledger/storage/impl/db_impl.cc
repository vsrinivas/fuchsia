// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/db_impl.h"

#include <algorithm>
#include <string>

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/impl/journal_db_impl.h"
#include "apps/ledger/src/storage/impl/object_impl.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/strings/concatenate.h"

#define RETURN_ON_ERROR(expr)   \
  do {                          \
    Status status = (expr);     \
    if (status != Status::OK) { \
      return status;            \
    }                           \
  } while (0)

namespace storage {

namespace {

constexpr ftl::StringView kHeadPrefix = "heads/";
constexpr ftl::StringView kCommitPrefix = "commits/";
constexpr ftl::StringView kObjectPrefix = "objects/";

// Journal keys
const size_t kJournalIdSize = 16;
constexpr ftl::StringView kJournalPrefix = "journals/";
constexpr ftl::StringView kImplicitJournalMetaPrefix = "journals/implicit/";
constexpr ftl::StringView kJournalEntry = "entry/";
constexpr ftl::StringView kJournalCounter = "counter/";
const char kImplicitJournalIdPrefix = 'I';
const char kExplicitJournalIdPrefix = 'E';
const size_t kJournalEntryPrefixSize =
    kJournalPrefix.size() + kJournalIdSize + 1 + kJournalEntry.size();
// Journal values
const char kJournalEntryAdd = 'A';
constexpr ftl::StringView kJournalEntryDelete = "D";
const char kJournalLazyEntry = 'L';
const char kJournalEagerEntry = 'E';
const size_t kJournalEntryAddPrefixSize = 2;

constexpr ftl::StringView kUnsyncedCommitPrefix = "unsynced/commits/";
constexpr ftl::StringView kTransientObjectPrefix = "transient/object_ids/";
constexpr ftl::StringView kLocalObjectPrefix = "local/object_ids/";

constexpr ftl::StringView kSyncMetadataPrefix = "sync-metadata/";

template <typename I>
I DeserializeNumber(ftl::StringView value) {
  FTL_DCHECK(value.size() == sizeof(I));
  return *reinterpret_cast<const I*>(value.data());
}

template <typename I>
ftl::StringView SerializeNumber(const I& value) {
  return ftl::StringView(reinterpret_cast<const char*>(&value), sizeof(I));
}

void ExtractSortedCommitsIds(
    std::vector<std::pair<std::string, std::string>>* entries,
    std::vector<CommitId>* commit_ids) {
  std::sort(entries->begin(), entries->end(),
            [](const std::pair<std::string, std::string>& p1,
               const std::pair<std::string, std::string>& p2) {
              auto t1 = DeserializeNumber<int64_t>(p1.second);
              auto t2 = DeserializeNumber<int64_t>(p2.second);
              if (t1 != t2) {
                return t1 < t2;
              }
              return p1.first < p2.first;
            });
  commit_ids->clear();
  commit_ids->reserve(entries->size());
  for (std::pair<std::string, std::string>& entry : *entries) {
    commit_ids->push_back(std::move(entry.first));
  }
}

std::string GetHeadKeyFor(CommitIdView head) {
  return ftl::Concatenate({kHeadPrefix, head});
}

std::string GetCommitKeyFor(CommitIdView commit_id) {
  return ftl::Concatenate({kCommitPrefix, commit_id});
}

std::string GetObjectKeyFor(ObjectIdView object_id) {
  return ftl::Concatenate({kObjectPrefix, object_id});
}

std::string GetUnsyncedCommitKeyFor(const CommitId& commit_id) {
  return ftl::Concatenate({kUnsyncedCommitPrefix, commit_id});
}

std::string GetTransientObjectKeyFor(ObjectIdView object_id) {
  return ftl::Concatenate({kTransientObjectPrefix, object_id});
}

std::string GetLocalObjectKeyFor(ObjectIdView object_id) {
  return ftl::Concatenate({kLocalObjectPrefix, object_id});
}

std::string GetImplicitJournalMetaKeyFor(const JournalId& journal_id) {
  return ftl::Concatenate({kImplicitJournalMetaPrefix, journal_id});
}

std::string GetSyncMetadataKeyFor(ftl::StringView key) {
  return ftl::Concatenate({kSyncMetadataPrefix, key});
}

std::string GetJournalEntryPrefixFor(const JournalId& journal_id) {
  return ftl::Concatenate({kJournalPrefix, journal_id, "/", kJournalEntry});
}

std::string GetJournalEntryKeyFor(const JournalId id, ftl::StringView key) {
  return ftl::Concatenate({GetJournalEntryPrefixFor(id), key});
}

std::string GetJournalEntryValueFor(ftl::StringView value,
                                    KeyPriority priority) {
  char priority_byte =
      (priority == KeyPriority::EAGER) ? kJournalEagerEntry : kJournalLazyEntry;
  return ftl::Concatenate({{&kJournalEntryAdd, 1}, {&priority_byte, 1}, value});
}

Status ExtractObjectId(ftl::StringView db_value, ObjectId* id) {
  if (db_value[0] == kJournalEntryDelete[0]) {
    return Status::NOT_FOUND;
  }
  *id = db_value.substr(kJournalEntryAddPrefixSize).ToString();
  return Status::OK;
}

std::string GetJournalCounterPrefixFor(const JournalId& id) {
  return ftl::Concatenate({kJournalPrefix, id, "/", kJournalCounter});
}

std::string GetJournalCounterKeyFor(const JournalId& id,
                                    ftl::StringView value) {
  return ftl::Concatenate({GetJournalCounterPrefixFor(id), value});
}

std::string NewJournalId(JournalType journal_type) {
  std::string id;
  id.resize(kJournalIdSize);
  id[0] = (journal_type == JournalType::IMPLICIT ? kImplicitJournalIdPrefix
                                                 : kExplicitJournalIdPrefix);
  glue::RandBytes(&id[1], kJournalIdSize - 1);
  return id;
}

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

class JournalEntryIterator : public Iterator<const EntryChange> {
 public:
  JournalEntryIterator(std::unique_ptr<leveldb::Iterator> it,
                       const std::string& prefix)
      : it_(std::move(it)), prefix_(prefix) {
    PrepareEntry();
  }

  ~JournalEntryIterator() override {}

  Iterator<const EntryChange>& Next() override {
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

  const EntryChange& operator*() const override { return *(change_.get()); }
  const EntryChange* operator->() const override { return change_.get(); }

 private:
  void PrepareEntry() {
    if (!Valid()) {
      change_.reset(nullptr);
      return;
    }
    change_ = std::make_unique<EntryChange>();

    leveldb::Slice key_slice = it_->key();
    key_slice.remove_prefix(kJournalEntryPrefixSize);
    change_->entry.key = key_slice.ToString();

    leveldb::Slice value = it_->value();
    if (value.data()[0] == kJournalEntryAdd) {
      change_->deleted = false;
      change_->entry.priority = (value.data()[1] == kJournalLazyEntry)
                                    ? KeyPriority::LAZY
                                    : KeyPriority::EAGER;

      value.remove_prefix(kJournalEntryAddPrefixSize);
      change_->entry.object_id = value.ToString();
    } else {
      change_->deleted = true;
    }
  }

  std::unique_ptr<leveldb::Iterator> it_;
  const std::string prefix_;

  std::unique_ptr<EntryChange> change_;
};

class BatchImpl : public DB::Batch {
 public:
  BatchImpl(std::function<Status(bool)> callback)
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

class EmptyBatch : public DB::Batch {
 public:
  EmptyBatch() {}

  ~EmptyBatch() override {}

  Status Execute() override { return Status::OK; }
};

}  // namespace

DbImpl::DbImpl(coroutine::CoroutineService* coroutine_service,
               PageStorageImpl* page_storage,
               std::string db_path)
    : coroutine_service_(coroutine_service),
      page_storage_(page_storage),
      db_path_(db_path) {
  FTL_DCHECK(page_storage);
}

DbImpl::~DbImpl() {
  FTL_DCHECK(!batch_);
}

Status DbImpl::Init() {
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

std::unique_ptr<DB::Batch> DbImpl::StartBatch() {
  FTL_DCHECK(!batch_);
  batch_ = std::make_unique<leveldb::WriteBatch>();
  return std::make_unique<BatchImpl>([this](bool execute) {
    std::unique_ptr<leveldb::WriteBatch> batch = std::move(batch_);
    if (execute) {
      leveldb::Status status = db_->Write(write_options_, batch.get());
      if (!status.ok()) {
        FTL_LOG(ERROR) << "Fail to execute batch with status: "
                       << status.ToString();
        return Status::INTERNAL_IO_ERROR;
      }
    }
    return Status::OK;
  });
}

Status DbImpl::GetHeads(std::vector<CommitId>* heads) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(GetEntriesByPrefix(convert::ToSlice(kHeadPrefix), &entries));
  ExtractSortedCommitsIds(&entries, heads);
  return Status::OK;
}

Status DbImpl::AddHead(CommitIdView head, int64_t timestamp) {
  return Put(GetHeadKeyFor(head), SerializeNumber(timestamp));
}

Status DbImpl::RemoveHead(CommitIdView head) {
  return Delete(GetHeadKeyFor(head));
}

Status DbImpl::GetCommitStorageBytes(CommitIdView commit_id,
                                     std::string* storage_bytes) {
  return Get(GetCommitKeyFor(commit_id), storage_bytes);
}

Status DbImpl::AddCommitStorageBytes(const CommitId& commit_id,
                                     ftl::StringView storage_bytes) {
  return Put(GetCommitKeyFor(commit_id), storage_bytes);
}

Status DbImpl::RemoveCommit(const CommitId& commit_id) {
  return Delete(GetCommitKeyFor(commit_id));
}

Status DbImpl::CreateJournal(JournalType journal_type,
                             const CommitId& base,
                             std::unique_ptr<Journal>* journal) {
  JournalId id = NewJournalId(journal_type);
  *journal = JournalDBImpl::Simple(journal_type, coroutine_service_,
                                   page_storage_, this, id, base);
  if (journal_type == JournalType::IMPLICIT) {
    return Put(GetImplicitJournalMetaKeyFor(id), base);
  }
  return Status::OK;
}

Status DbImpl::CreateMergeJournal(const CommitId& base,
                                  const CommitId& other,
                                  std::unique_ptr<Journal>* journal) {
  *journal =
      JournalDBImpl::Merge(coroutine_service_, page_storage_, this,
                           NewJournalId(JournalType::EXPLICIT), base, other);
  return Status::OK;
}

Status DbImpl::GetImplicitJournalIds(std::vector<JournalId>* journal_ids) {
  return GetByPrefix(convert::ToSlice(kImplicitJournalMetaPrefix), journal_ids);
}

Status DbImpl::GetImplicitJournal(const JournalId& journal_id,
                                  std::unique_ptr<Journal>* journal) {
  FTL_DCHECK(journal_id.size() == kJournalIdSize);
  FTL_DCHECK(journal_id[0] == kImplicitJournalIdPrefix);
  CommitId base;
  RETURN_ON_ERROR(Get(GetImplicitJournalMetaKeyFor(journal_id), &base));
  *journal = JournalDBImpl::Simple(JournalType::IMPLICIT, coroutine_service_,
                                   page_storage_, this, journal_id, base);
  return Status::OK;
}

Status DbImpl::RemoveExplicitJournals() {
  static std::string kExplicitJournalPrefix = ftl::Concatenate(
      {kJournalPrefix, ftl::StringView(&kImplicitJournalIdPrefix, 1)});
  return DeleteByPrefix(kExplicitJournalPrefix);
}

Status DbImpl::RemoveJournal(const JournalId& journal_id) {
  if (journal_id[0] == kImplicitJournalIdPrefix) {
    RETURN_ON_ERROR(Delete(GetImplicitJournalMetaKeyFor(journal_id)));
  }
  return DeleteByPrefix(GetJournalEntryPrefixFor(journal_id));
}

Status DbImpl::AddJournalEntry(const JournalId& journal_id,
                               ftl::StringView key,
                               ftl::StringView value,
                               KeyPriority priority) {
  return Put(GetJournalEntryKeyFor(journal_id, key),
             GetJournalEntryValueFor(value, priority));
}

Status DbImpl::RemoveJournalEntry(const JournalId& journal_id,
                                  convert::ExtendedStringView key) {
  return Put(GetJournalEntryKeyFor(journal_id, key), kJournalEntryDelete);
}

Status DbImpl::GetJournalValue(const JournalId& journal_id,
                               ftl::StringView key,
                               std::string* value) {
  std::string db_value;
  RETURN_ON_ERROR(Get(GetJournalEntryKeyFor(journal_id, key), &db_value));
  return ExtractObjectId(db_value, value);
}

Status DbImpl::GetJournalEntries(
    const JournalId& journal_id,
    std::unique_ptr<Iterator<const EntryChange>>* entries) {
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  std::string prefix = GetJournalEntryPrefixFor(journal_id);
  it->Seek(prefix);

  *entries = std::make_unique<JournalEntryIterator>(std::move(it), prefix);
  return Status::OK;
}

Status DbImpl::WriteObject(ObjectIdView object_id,
                           std::unique_ptr<DataSource::DataChunk> content,
                           ObjectStatus object_status) {
  FTL_DCHECK(object_status > ObjectStatus::UNKNOWN);

  auto object_key = GetObjectKeyFor(object_id);
  bool has_key;
  RETURN_ON_ERROR(HasKey(object_key, &has_key));
  if (has_key && object_status > ObjectStatus::TRANSIENT) {
    return SetObjectStatus(object_id, object_status);
  }

  auto batch = StartLocalBatch();

  Put(object_key, content->Get());
  switch (object_status) {
    case ObjectStatus::UNKNOWN:
      FTL_NOTREACHED();
      break;
    case ObjectStatus::TRANSIENT:
      Put(GetTransientObjectKeyFor(object_id), "");
      break;
    case ObjectStatus::LOCAL:
      Put(GetLocalObjectKeyFor(object_id), "");
      break;
    case ObjectStatus::SYNCED:
      // Nothing to do.
      break;
  }
  return batch->Execute();
}

Status DbImpl::ReadObject(ObjectId object_id,
                          std::unique_ptr<const Object>* object) {
  std::unique_ptr<leveldb::Iterator> iterator;
  RETURN_ON_ERROR(GetIteratorAt(GetObjectKeyFor(object_id), &iterator));
  if (object) {
    *object = std::make_unique<LevelDBObject>(std::move(object_id),
                                              std::move(iterator));
  }
  return Status::OK;
}

Status DbImpl::DeleteObject(ObjectIdView object_id) {
  auto batch = StartLocalBatch();
  Delete(GetObjectKeyFor(object_id));
  Delete(GetTransientObjectKeyFor(object_id));
  Delete(GetLocalObjectKeyFor(object_id));
  return batch->Execute();
}

Status DbImpl::GetJournalValueCounter(const JournalId& journal_id,
                                      ftl::StringView value,
                                      int64_t* counter) {
  std::string counter_str;
  Status status = Get(GetJournalCounterKeyFor(journal_id, value), &counter_str);
  if (status == Status::NOT_FOUND) {
    *counter = 0;
    return Status::OK;
  }
  if (status != Status::OK) {
    return status;
  }
  *counter = DeserializeNumber<int64_t>(counter_str);
  return Status::OK;
}

Status DbImpl::SetJournalValueCounter(const JournalId& journal_id,
                                      ftl::StringView value,
                                      int64_t counter) {
  FTL_DCHECK(counter >= 0);
  if (counter == 0) {
    return Delete(GetJournalCounterKeyFor(journal_id, value));
  }
  return Put(GetJournalCounterKeyFor(journal_id, value),
             SerializeNumber(counter));
}
Status DbImpl::GetJournalValues(const JournalId& journal_id,
                                std::vector<std::string>* values) {
  return GetByPrefix(GetJournalCounterPrefixFor(journal_id), values);
}

Status DbImpl::GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(
      GetEntriesByPrefix(convert::ToSlice(kUnsyncedCommitPrefix), &entries));
  ExtractSortedCommitsIds(&entries, commit_ids);
  return Status::OK;
}

Status DbImpl::MarkCommitIdSynced(const CommitId& commit_id) {
  return Delete(GetUnsyncedCommitKeyFor(commit_id));
}

Status DbImpl::MarkCommitIdUnsynced(const CommitId& commit_id,
                                    int64_t timestamp) {
  return Put(GetUnsyncedCommitKeyFor(commit_id), SerializeNumber(timestamp));
}

Status DbImpl::IsCommitSynced(const CommitId& commit_id, bool* is_synced) {
  bool has_key;
  RETURN_ON_ERROR(HasKey(GetUnsyncedCommitKeyFor(commit_id), &has_key));
  *is_synced = !has_key;
  return Status::OK;
}

Status DbImpl::GetUnsyncedPieces(std::vector<ObjectId>* object_ids) {
  return GetByPrefix(convert::ToSlice(kLocalObjectPrefix), object_ids);
}

Status DbImpl::SetObjectStatus(ObjectIdView object_id,
                               ObjectStatus object_status) {
  FTL_DCHECK(object_status >= ObjectStatus::LOCAL);
  FTL_DCHECK(CheckHasKey(GetObjectKeyFor(object_id)))
      << "Unknown object: " << convert::ToHex(object_id);

  auto transient_key = GetTransientObjectKeyFor(object_id);
  auto local_key = GetLocalObjectKeyFor(object_id);

  auto batch = StartLocalBatch();
  bool has_key;

  switch (object_status) {
    case ObjectStatus::UNKNOWN:
    case ObjectStatus::TRANSIENT:
      FTL_NOTREACHED();
      break;
    case ObjectStatus::LOCAL:
      RETURN_ON_ERROR(HasKey(transient_key, &has_key));
      if (has_key) {
        Delete(transient_key);
        Put(local_key, "");
      }
      break;
    case ObjectStatus::SYNCED:
      Delete(local_key);
      Delete(transient_key);
      break;
  }

  return batch->Execute();
}

Status DbImpl::GetObjectStatus(ObjectIdView object_id,
                               ObjectStatus* object_status) {
  bool has_key;

  RETURN_ON_ERROR(HasKey(GetLocalObjectKeyFor(object_id), &has_key));
  if (has_key) {
    *object_status = ObjectStatus::LOCAL;
    return Status::OK;
  }

  RETURN_ON_ERROR(HasKey(GetTransientObjectKeyFor(object_id), &has_key));
  if (has_key) {
    *object_status = ObjectStatus::TRANSIENT;
    return Status::OK;
  }

  RETURN_ON_ERROR(HasKey(GetObjectKeyFor(object_id), &has_key));
  if (!has_key) {
    *object_status = ObjectStatus::UNKNOWN;
    return Status::OK;
  }

  *object_status = ObjectStatus::SYNCED;
  return Status::OK;
}

Status DbImpl::SetSyncMetadata(ftl::StringView key, ftl::StringView value) {
  return Put(GetSyncMetadataKeyFor(key), value);
}

Status DbImpl::GetSyncMetadata(ftl::StringView key, std::string* value) {
  return Get(GetSyncMetadataKeyFor(key), value);
}

std::unique_ptr<DB::Batch> DbImpl::StartLocalBatch() {
  if (batch_) {
    return std::make_unique<EmptyBatch>();
  }
  return StartBatch();
}

Status DbImpl::GetByPrefix(const leveldb::Slice& prefix,
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

Status DbImpl::GetEntriesByPrefix(
    const leveldb::Slice& prefix,
    std::vector<std::pair<std::string, std::string>>* key_value_pairs) {
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
  key_value_pairs->swap(result);
  return Status::OK;
}

Status DbImpl::DeleteByPrefix(const leveldb::Slice& prefix) {
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix);
       it->Next()) {
    Delete(it->key());
  }
  return ConvertStatus(it->status());
}

Status DbImpl::Get(convert::ExtendedStringView key, std::string* value) {
  return ConvertStatus(db_->Get(read_options_, key, value));
}

Status DbImpl::Put(convert::ExtendedStringView key, ftl::StringView value) {
  if (batch_) {
    batch_->Put(key, convert::ToSlice(value));
    return Status::OK;
  }
  return ConvertStatus(db_->Put(write_options_, key, convert::ToSlice(value)));
}

Status DbImpl::Delete(convert::ExtendedStringView key) {
  if (batch_) {
    batch_->Delete(key);
    return Status::OK;
  }
  return ConvertStatus(db_->Delete(write_options_, key));
}

Status DbImpl::GetIteratorAt(convert::ExtendedStringView key,
                             std::unique_ptr<leveldb::Iterator>* iterator) {
  std::unique_ptr<leveldb::Iterator> local_iterator(
      db_->NewIterator(read_options_));
  local_iterator->Seek(key);

  if (!local_iterator->Valid() || local_iterator->key() != key) {
    return Status::NOT_FOUND;
  }

  if (iterator) {
    iterator->swap(local_iterator);
  }
  return Status::OK;
}

Status DbImpl::HasKey(convert::ExtendedStringView key, bool* has_key) {
  Status status = GetIteratorAt(key, nullptr);
  if (status == Status::OK || status == Status::NOT_FOUND) {
    *has_key = (status == Status::OK);
    return Status::OK;
  }
  return status;
}

bool DbImpl::CheckHasKey(convert::ExtendedStringView key) {
  bool result;
  Status status = HasKey(key, &result);
  if (status != Status::OK) {
    return false;
  }
  return result;
}

}  // namespace storage
