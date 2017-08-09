// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_db_impl.h"

#include <algorithm>
#include <string>

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/ledger/src/storage/impl/journal_db_impl.h"
#include "apps/ledger/src/storage/impl/object_impl.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
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

class JournalEntryIterator : public Iterator<const EntryChange> {
 public:
  explicit JournalEntryIterator(
      std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                               convert::ExtendedStringView>>>
          it)
      : it_(std::move(it)) {
    PrepareEntry();
  }

  ~JournalEntryIterator() override {}

  Iterator<const EntryChange>& Next() override {
    it_->Next();
    PrepareEntry();
    return *this;
  }

  bool Valid() const override { return it_->Valid(); }

  Status GetStatus() const override { return it_->GetStatus(); }

  const EntryChange& operator*() const override { return *(change_.get()); }
  const EntryChange* operator->() const override { return change_.get(); }

 private:
  void PrepareEntry() {
    if (!Valid()) {
      change_.reset(nullptr);
      return;
    }
    change_ = std::make_unique<EntryChange>();

    const std::pair<convert::ExtendedStringView, convert::ExtendedStringView>&
        key_value = **it_;
    change_->entry.key =
        key_value.first.substr(kJournalEntryPrefixSize).ToString();

    if (key_value.second[0] == kJournalEntryAdd) {
      change_->deleted = false;
      change_->entry.priority = (key_value.second[1] == kJournalLazyEntry)
                                    ? KeyPriority::LAZY
                                    : KeyPriority::EAGER;

      change_->entry.object_id =
          key_value.second.substr(kJournalEntryAddPrefixSize).ToString();
    } else {
      change_->deleted = true;
    }
  }

  std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                           convert::ExtendedStringView>>>
      it_;

  std::unique_ptr<EntryChange> change_;
};

class BatchImpl : public PageDb::Batch {
 public:
  explicit BatchImpl(std::unique_ptr<Db::Batch> batch)
      : batch_(std::move(batch)) {}

  ~BatchImpl() override {}

  Status Execute() override { return batch_->Execute(); }

 private:
  std::unique_ptr<Db::Batch> batch_;
};

class EmptyBatch : public PageDb::Batch {
 public:
  EmptyBatch() {}

  ~EmptyBatch() override {}

  Status Execute() override { return Status::OK; }
};

}  // namespace

PageDbImpl::PageDbImpl(coroutine::CoroutineService* coroutine_service,
                       PageStorageImpl* page_storage,
                       std::string db_path)
    : coroutine_service_(coroutine_service),
      page_storage_(page_storage),
      db_(std::move(db_path)) {
  FTL_DCHECK(page_storage);
}

PageDbImpl::~PageDbImpl() {}

Status PageDbImpl::Init() {
  return db_.Init();
}

std::unique_ptr<PageDb::Batch> PageDbImpl::StartBatch() {
  return std::make_unique<BatchImpl>(db_.StartBatch());
}

Status PageDbImpl::GetHeads(std::vector<CommitId>* heads) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(
      db_.GetEntriesByPrefix(convert::ToSlice(kHeadPrefix), &entries));
  ExtractSortedCommitsIds(&entries, heads);
  return Status::OK;
}

Status PageDbImpl::AddHead(CommitIdView head, int64_t timestamp) {
  return db_.Put(GetHeadKeyFor(head), SerializeNumber(timestamp));
}

Status PageDbImpl::RemoveHead(CommitIdView head) {
  return db_.Delete(GetHeadKeyFor(head));
}

Status PageDbImpl::GetCommitStorageBytes(CommitIdView commit_id,
                                         std::string* storage_bytes) {
  return db_.Get(GetCommitKeyFor(commit_id), storage_bytes);
}

Status PageDbImpl::AddCommitStorageBytes(const CommitId& commit_id,
                                         ftl::StringView storage_bytes) {
  return db_.Put(GetCommitKeyFor(commit_id), storage_bytes);
}

Status PageDbImpl::RemoveCommit(const CommitId& commit_id) {
  return db_.Delete(GetCommitKeyFor(commit_id));
}

Status PageDbImpl::CreateJournal(JournalType journal_type,
                                 const CommitId& base,
                                 std::unique_ptr<Journal>* journal) {
  JournalId id = NewJournalId(journal_type);
  *journal = JournalDBImpl::Simple(journal_type, coroutine_service_,
                                   page_storage_, this, id, base);
  if (journal_type == JournalType::IMPLICIT) {
    return db_.Put(GetImplicitJournalMetaKeyFor(id), base);
  }
  return Status::OK;
}

Status PageDbImpl::CreateMergeJournal(const CommitId& base,
                                      const CommitId& other,
                                      std::unique_ptr<Journal>* journal) {
  *journal =
      JournalDBImpl::Merge(coroutine_service_, page_storage_, this,
                           NewJournalId(JournalType::EXPLICIT), base, other);
  return Status::OK;
}

Status PageDbImpl::GetImplicitJournalIds(std::vector<JournalId>* journal_ids) {
  return db_.GetByPrefix(convert::ToSlice(kImplicitJournalMetaPrefix),
                         journal_ids);
}

Status PageDbImpl::GetImplicitJournal(const JournalId& journal_id,
                                      std::unique_ptr<Journal>* journal) {
  FTL_DCHECK(journal_id.size() == kJournalIdSize);
  FTL_DCHECK(journal_id[0] == kImplicitJournalIdPrefix);
  CommitId base;
  RETURN_ON_ERROR(db_.Get(GetImplicitJournalMetaKeyFor(journal_id), &base));
  *journal = JournalDBImpl::Simple(JournalType::IMPLICIT, coroutine_service_,
                                   page_storage_, this, journal_id, base);
  return Status::OK;
}

Status PageDbImpl::RemoveExplicitJournals() {
  static std::string kExplicitJournalPrefix = ftl::Concatenate(
      {kJournalPrefix, ftl::StringView(&kImplicitJournalIdPrefix, 1)});
  return db_.DeleteByPrefix(kExplicitJournalPrefix);
}

Status PageDbImpl::RemoveJournal(const JournalId& journal_id) {
  if (journal_id[0] == kImplicitJournalIdPrefix) {
    RETURN_ON_ERROR(db_.Delete(GetImplicitJournalMetaKeyFor(journal_id)));
  }
  return db_.DeleteByPrefix(GetJournalEntryPrefixFor(journal_id));
}

Status PageDbImpl::AddJournalEntry(const JournalId& journal_id,
                                   ftl::StringView key,
                                   ftl::StringView value,
                                   KeyPriority priority) {
  return db_.Put(GetJournalEntryKeyFor(journal_id, key),
                 GetJournalEntryValueFor(value, priority));
}

Status PageDbImpl::RemoveJournalEntry(const JournalId& journal_id,
                                      convert::ExtendedStringView key) {
  return db_.Put(GetJournalEntryKeyFor(journal_id, key), kJournalEntryDelete);
}

Status PageDbImpl::GetJournalValue(const JournalId& journal_id,
                                   ftl::StringView key,
                                   std::string* value) {
  std::string db_value;
  RETURN_ON_ERROR(db_.Get(GetJournalEntryKeyFor(journal_id, key), &db_value));
  return ExtractObjectId(db_value, value);
}

Status PageDbImpl::GetJournalEntries(
    const JournalId& journal_id,
    std::unique_ptr<Iterator<const EntryChange>>* entries) {
  std::unique_ptr<Iterator<const std::pair<convert::ExtendedStringView,
                                           convert::ExtendedStringView>>>
      it;
  RETURN_ON_ERROR(
      db_.GetIteratorAtPrefix(GetJournalEntryPrefixFor(journal_id), &it));

  *entries = std::make_unique<JournalEntryIterator>(std::move(it));
  return Status::OK;
}

Status PageDbImpl::WriteObject(ObjectIdView object_id,
                               std::unique_ptr<DataSource::DataChunk> content,
                               ObjectStatus object_status) {
  FTL_DCHECK(object_status > ObjectStatus::UNKNOWN);

  auto object_key = GetObjectKeyFor(object_id);
  bool has_key;
  RETURN_ON_ERROR(db_.HasKey(object_key, &has_key));
  if (has_key && object_status > ObjectStatus::TRANSIENT) {
    return SetObjectStatus(object_id, object_status);
  }

  auto batch = StartLocalBatch();

  db_.Put(object_key, content->Get());
  switch (object_status) {
    case ObjectStatus::UNKNOWN:
      FTL_NOTREACHED();
      break;
    case ObjectStatus::TRANSIENT:
      db_.Put(GetTransientObjectKeyFor(object_id), "");
      break;
    case ObjectStatus::LOCAL:
      db_.Put(GetLocalObjectKeyFor(object_id), "");
      break;
    case ObjectStatus::SYNCED:
      // Nothing to do.
      break;
  }
  return batch->Execute();
}

Status PageDbImpl::ReadObject(ObjectId object_id,
                              std::unique_ptr<const Object>* object) {
  return db_.GetObject(GetObjectKeyFor(object_id), object_id, object);
}

Status PageDbImpl::DeleteObject(ObjectIdView object_id) {
  auto batch = StartLocalBatch();
  db_.Delete(GetObjectKeyFor(object_id));
  db_.Delete(GetTransientObjectKeyFor(object_id));
  db_.Delete(GetLocalObjectKeyFor(object_id));
  return batch->Execute();
}

Status PageDbImpl::GetJournalValueCounter(const JournalId& journal_id,
                                          ftl::StringView value,
                                          int64_t* counter) {
  std::string counter_str;
  Status status =
      db_.Get(GetJournalCounterKeyFor(journal_id, value), &counter_str);
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

Status PageDbImpl::SetJournalValueCounter(const JournalId& journal_id,
                                          ftl::StringView value,
                                          int64_t counter) {
  FTL_DCHECK(counter >= 0);
  if (counter == 0) {
    return db_.Delete(GetJournalCounterKeyFor(journal_id, value));
  }
  return db_.Put(GetJournalCounterKeyFor(journal_id, value),
                 SerializeNumber(counter));
}

Status PageDbImpl::GetJournalValues(const JournalId& journal_id,
                                    std::vector<std::string>* values) {
  return db_.GetByPrefix(GetJournalCounterPrefixFor(journal_id), values);
}

Status PageDbImpl::GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids) {
  std::vector<std::pair<std::string, std::string>> entries;
  RETURN_ON_ERROR(db_.GetEntriesByPrefix(
      convert::ToSlice(kUnsyncedCommitPrefix), &entries));
  ExtractSortedCommitsIds(&entries, commit_ids);
  return Status::OK;
}

Status PageDbImpl::MarkCommitIdSynced(const CommitId& commit_id) {
  return db_.Delete(GetUnsyncedCommitKeyFor(commit_id));
}

Status PageDbImpl::MarkCommitIdUnsynced(const CommitId& commit_id,
                                        uint64_t generation) {
  return db_.Put(GetUnsyncedCommitKeyFor(commit_id),
                 SerializeNumber(generation));
}

Status PageDbImpl::IsCommitSynced(const CommitId& commit_id, bool* is_synced) {
  bool has_key;
  RETURN_ON_ERROR(db_.HasKey(GetUnsyncedCommitKeyFor(commit_id), &has_key));
  *is_synced = !has_key;
  return Status::OK;
}

Status PageDbImpl::GetUnsyncedPieces(std::vector<ObjectId>* object_ids) {
  return db_.GetByPrefix(convert::ToSlice(kLocalObjectPrefix), object_ids);
}

Status PageDbImpl::SetObjectStatus(ObjectIdView object_id,
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
      RETURN_ON_ERROR(db_.HasKey(transient_key, &has_key));
      if (has_key) {
        db_.Delete(transient_key);
        db_.Put(local_key, "");
      }
      break;
    case ObjectStatus::SYNCED:
      db_.Delete(local_key);
      db_.Delete(transient_key);
      break;
  }

  return batch->Execute();
}

Status PageDbImpl::GetObjectStatus(ObjectIdView object_id,
                                   ObjectStatus* object_status) {
  bool has_key;

  RETURN_ON_ERROR(db_.HasKey(GetLocalObjectKeyFor(object_id), &has_key));
  if (has_key) {
    *object_status = ObjectStatus::LOCAL;
    return Status::OK;
  }

  RETURN_ON_ERROR(db_.HasKey(GetTransientObjectKeyFor(object_id), &has_key));
  if (has_key) {
    *object_status = ObjectStatus::TRANSIENT;
    return Status::OK;
  }

  RETURN_ON_ERROR(db_.HasKey(GetObjectKeyFor(object_id), &has_key));
  if (!has_key) {
    *object_status = ObjectStatus::UNKNOWN;
    return Status::OK;
  }

  *object_status = ObjectStatus::SYNCED;
  return Status::OK;
}

Status PageDbImpl::SetSyncMetadata(ftl::StringView key, ftl::StringView value) {
  return db_.Put(GetSyncMetadataKeyFor(key), value);
}

Status PageDbImpl::GetSyncMetadata(ftl::StringView key, std::string* value) {
  return db_.Get(GetSyncMetadataKeyFor(key), value);
}

std::unique_ptr<PageDb::Batch> PageDbImpl::StartLocalBatch() {
  if (db_.BatchStarted()) {
    return std::make_unique<EmptyBatch>();
  }
  return std::make_unique<BatchImpl>(db_.StartBatch());
}

bool PageDbImpl::CheckHasKey(convert::ExtendedStringView key) {
  bool result;
  Status status = db_.HasKey(key, &result);
  if (status != Status::OK) {
    return false;
  }
  return result;
}

}  // namespace storage
