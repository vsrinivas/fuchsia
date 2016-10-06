// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/db.h"

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/impl/journal_db_impl.h"
#include "lib/ftl/files/directory.h"

namespace storage {

namespace {

const char kHeadPrefix[] = "heads/";
const char kCommitPrefix[] = "commits/";

// Journal keys
const size_t kJournalIdSize = 16;
const char kJournalPrefix[] = "journals/";
const char kImplicitJournalMetaPrefix[] = "journals/implicit/";
const char kImplicitJournalIdPrefix = 'I';
const char kExplicitJournalIdPrefix = 'E';
// Journal values
const char kJournalEntryAdd = 'A';
const char kJournalEntryDelete[] = "D";
const char kJournalLazyEntry = 'L';
const char kJournalEagerEntry = 'E';

const char kUnsyncedCommitPrefix[] = "unsynced/commits/";
const char kUnsyncedObjectPrefix[] = "unsynced/objects/";

std::string GetHeadKeyFor(const CommitId& head) {
  return kHeadPrefix + head;
}

std::string GetCommitKeyFor(const CommitId& commit_id) {
  return kCommitPrefix + commit_id;
}

std::string GetUnsyncedCommitKeyFor(const CommitId& commit_id) {
  return kUnsyncedCommitPrefix + commit_id;
}

std::string GetUnsyncedObjectKeyFor(const ObjectId& object_id) {
  return kUnsyncedObjectPrefix + object_id;
}

std::string GetImplicitJournalMetaKeyFor(const JournalId& journal_id) {
  return kImplicitJournalMetaPrefix + journal_id;
}

std::string GetJournalEntryPrefixFor(const JournalId& journal_id) {
  return kJournalPrefix + journal_id;
}

std::string GetJournalEntryKeyFor(const JournalId id, const std::string& key) {
  std::string result;
  result.reserve(sizeof(kJournalPrefix) + kJournalIdSize + key.size());
  return result.append(kJournalPrefix).append(id).append("/").append(key);
}

std::string GetJournalEntryValueFor(const std::string& value,
                                    KeyPriority priority) {
  std::string result;
  char priorityByte =
      (priority == KeyPriority::EAGER) ? kJournalEagerEntry : kJournalLazyEntry;
  result.reserve(value.size() + 2);
  return result.append(1, kJournalEntryAdd)
      .append(1, priorityByte)
      .append(value);
}

std::string NewJournalId(bool implicit) {
  std::string id;
  id.resize(kJournalIdSize);
  id[0] = (implicit ? kImplicitJournalIdPrefix : kExplicitJournalIdPrefix);
  glue::RandBytes(&id[1], kJournalIdSize - 1);
  return id;
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
    return it_->status().ok() ? Status::OK : Status::IO_ERROR;
  }

  const EntryChange& operator*() const override { return *(change_.get()); }
  const EntryChange* operator->() const override { return change_.get(); }

 private:
  void PrepareEntry() {
    if (!Valid()) {
      change_.reset(nullptr);
      return;
    }
    change_.reset(new EntryChange());

    static int journalPrefixLength = sizeof(kJournalPrefix) + kJournalIdSize;
    leveldb::Slice keySlice = it_->key();
    keySlice.remove_prefix(journalPrefixLength);
    change_->entry.key = keySlice.ToString();

    leveldb::Slice value = it_->value();
    if (value.data()[0] == kJournalEntryAdd) {
      change_->deleted = false;
      change_->entry.priority = (value.data()[1] == kJournalLazyEntry)
                                    ? KeyPriority::LAZY
                                    : KeyPriority::EAGER;

      value.remove_prefix(2);
      change_->entry.blob_id = value.ToString();
    } else {
      change_->deleted = true;
    }
  }

  std::unique_ptr<leveldb::Iterator> it_;
  const std::string prefix_;

  std::unique_ptr<EntryChange> change_;
};

}  // namespace

DB::DB(std::string db_path) : db_path_(db_path) {}

DB::~DB() {}

Status DB::Init() {
  if (!files::CreateDirectory(db_path_)) {
    FTL_LOG(ERROR) << "Failed to create directory under " << db_path_;
    return Status::IO_ERROR;
  }
  leveldb::DB* db = nullptr;
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::Status status = leveldb::DB::Open(options, db_path_, &db);
  if (!status.ok()) {
    FTL_LOG(ERROR) << "Failed to open ledger at " << db_path_
                   << " with status: " << status.ToString();
    return Status::IO_ERROR;
  }
  db_.reset(db);
  return Status::OK;
}

Status DB::GetHeads(std::vector<CommitId>* heads) {
  return GetByPrefix(leveldb::Slice(kHeadPrefix, sizeof(kHeadPrefix) - 1),
                     heads);
}

Status DB::AddHead(const CommitId& head) {
  return Put(GetHeadKeyFor(head), "");
}

Status DB::RemoveHead(const CommitId& head) {
  return Delete(GetHeadKeyFor(head));
}

Status DB::ContainsHead(const CommitId& commit_id) {
  std::string value;
  return Get(GetHeadKeyFor(commit_id), &value);
}

Status DB::GetCommitStorageBytes(const CommitId& commit_id,
                                 std::string* storage_bytes) {
  return Get(GetCommitKeyFor(commit_id), storage_bytes);
}

Status DB::AddCommitStorageBytes(const CommitId& commit_id,
                                 const std::string& storage_bytes) {
  return Put(GetCommitKeyFor(commit_id), storage_bytes);
}

Status DB::RemoveCommit(const CommitId& commit_id) {
  return Delete(GetCommitKeyFor(commit_id));
}

Status DB::CreateJournal(bool implicit,
                         const CommitId& base,
                         std::unique_ptr<Journal>* journal) {
  JournalId id = NewJournalId(implicit);
  *journal = JournalDBImpl::Simple(this, id, base);
  if (implicit) {
    return Put(GetImplicitJournalMetaKeyFor(id), base);
  }
  return Status::OK;
}

Status DB::CreateMergeJournal(const CommitId& base,
                              const CommitId& other,
                              std::unique_ptr<Journal>* journal) {
  *journal = JournalDBImpl::Merge(this, NewJournalId(false), base, other);
  return Status::OK;
}

Status DB::GetImplicitJournalIds(std::vector<JournalId>* journal_ids) {
  return GetByPrefix(leveldb::Slice(kImplicitJournalMetaPrefix,
                                    sizeof(kImplicitJournalMetaPrefix) - 1),
                     journal_ids);
}

Status DB::GetImplicitJournal(const JournalId& journal_id,
                              std::unique_ptr<Journal>* journal) {
  FTL_DCHECK(journal_id.size() == kJournalIdSize);
  FTL_DCHECK(journal_id[0] == kImplicitJournalIdPrefix);
  CommitId base;
  Status s = Get(GetImplicitJournalMetaKeyFor(journal_id), &base);
  if (s == Status::OK) {
    *journal = JournalDBImpl::Simple(this, journal_id, base);
  }
  return s;
}

Status DB::RemoveExplicitJournals() {
  static std::string kExplicitJournalPrefix =
      std::string(kJournalPrefix).append(1, kImplicitJournalIdPrefix);
  return DeleteByPrefix(kExplicitJournalPrefix);
}

Status DB::RemoveJournal(const JournalId& journal_id) {
  if (journal_id[0] == kImplicitJournalIdPrefix) {
    Status s = Delete(GetImplicitJournalMetaKeyFor(journal_id));
    if (s != Status::OK) {
      return s;
    }
  }
  return DeleteByPrefix(GetJournalEntryPrefixFor(journal_id));
}

Status DB::AddJournalEntry(const JournalId& journal_id,
                           const std::string& key,
                           const std::string& value,
                           KeyPriority priority) {
  return Put(GetJournalEntryKeyFor(journal_id, key),
             GetJournalEntryValueFor(value, priority));
}

Status DB::RemoveJournalEntry(const JournalId& journal_id,
                              const std::string& key) {
  return Put(GetJournalEntryKeyFor(journal_id, key), kJournalEntryDelete);
}

Status DB::GetJournalEntries(
    const JournalId& journal_id,
    std::unique_ptr<Iterator<const EntryChange>>* entries) {
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  std::string prefix = GetJournalEntryPrefixFor(journal_id);
  it->Seek(prefix);

  entries->reset(new JournalEntryIterator(std::move(it), prefix));
  return Status::OK;
}

Status DB::GetUnsyncedCommitIds(std::vector<CommitId>* commit_ids) {
  return GetByPrefix(
      leveldb::Slice(kUnsyncedCommitPrefix, sizeof(kUnsyncedCommitPrefix) - 1),
      commit_ids);
}

Status DB::MarkCommitIdSynced(const CommitId& commit_id) {
  return Delete(GetUnsyncedCommitKeyFor(commit_id));
}

Status DB::MarkCommitIdUnsynced(const CommitId& commit_id) {
  return Put(GetUnsyncedCommitKeyFor(commit_id), "");
}

Status DB::IsCommitSynced(const CommitId& commit_id, bool* is_synced) {
  std::string value;
  Status s = Get(GetUnsyncedCommitKeyFor(commit_id), &value);
  if (s == Status::IO_ERROR) {
    return s;
  }
  *is_synced = (s == Status::NOT_FOUND);
  return Status::OK;
}

Status DB::GetUnsyncedObjectIds(std::vector<ObjectId>* object_ids) {
  return GetByPrefix(
      leveldb::Slice(kUnsyncedObjectPrefix, sizeof(kUnsyncedObjectPrefix) - 1),
      object_ids);
}

Status DB::MarkObjectIdSynced(const ObjectId& object_id) {
  return Delete(GetUnsyncedObjectKeyFor(object_id));
}

Status DB::MarkObjectIdUnsynced(const ObjectId& object_id) {
  return Put(GetUnsyncedObjectKeyFor(object_id), "");
}

Status DB::IsObjectSynced(const ObjectId& object_id, bool* is_synced) {
  std::string value;
  Status s = Get(GetUnsyncedObjectKeyFor(object_id), &value);
  if (s == Status::IO_ERROR) {
    return s;
  }
  *is_synced = (s == Status::NOT_FOUND);
  return Status::OK;
}

Status DB::GetByPrefix(const leveldb::Slice& prefix,
                       std::vector<std::string>* keySuffixes) {
  std::vector<std::string> result;
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix);
       it->Next()) {
    leveldb::Slice key = it->key();
    key.remove_prefix(prefix.size());
    result.push_back(key.ToString());
  }
  if (!it->status().ok()) {
    return Status::IO_ERROR;
  }
  keySuffixes->swap(result);
  return Status::OK;
}

Status DB::DeleteByPrefix(const leveldb::Slice& prefix) {
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix);
       it->Next()) {
    db_->Delete(write_options_, it->key());
  }
  return it->status().ok() ? Status::OK : Status::IO_ERROR;
}

Status DB::Get(const std::string& key, std::string* value) {
  leveldb::Status s = db_->Get(read_options_, key, value);
  if (s.IsNotFound()) {
    return Status::NOT_FOUND;
  }
  if (!s.ok()) {
    return Status::IO_ERROR;
  }
  return Status::OK;
}

Status DB::Put(const std::string& key, const std::string& value) {
  leveldb::Status s = db_->Put(write_options_, key, value);
  return s.ok() ? Status::OK : Status::IO_ERROR;
}

Status DB::Delete(const std::string& key) {
  leveldb::Status s = db_->Delete(write_options_, key);
  return s.ok() ? Status::OK : Status::IO_ERROR;
}

}  // namespace storage
