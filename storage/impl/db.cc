// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/db.h"

#include "apps/ledger/convert/convert.h"
#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/impl/journal_db_impl.h"
#include "lib/ftl/files/directory.h"

namespace storage {

namespace {

constexpr ftl::StringView kHeadPrefix = "heads/";
constexpr ftl::StringView kCommitPrefix = "commits/";

// Journal keys
const size_t kJournalIdSize = 16;
constexpr ftl::StringView kJournalPrefix = "journals/";
constexpr ftl::StringView kImplicitJournalMetaPrefix = "journals/implicit/";
const char kImplicitJournalIdPrefix = 'I';
const char kExplicitJournalIdPrefix = 'E';
// Journal values
const char kJournalEntryAdd = 'A';
constexpr ftl::StringView kJournalEntryDelete = "D";
const char kJournalLazyEntry = 'L';
const char kJournalEagerEntry = 'E';

constexpr ftl::StringView kUnsyncedCommitPrefix = "unsynced/commits/";
constexpr ftl::StringView kUnsyncedObjectPrefix = "unsynced/objects/";

std::string Concatenate(std::initializer_list<ftl::StringView> l) {
  std::string result;
  size_t result_size = 0;
  for (const ftl::StringView& s : l) {
    result_size += s.size();
  }
  result.reserve(result_size);
  for (const ftl::StringView& s : l) {
    result.append(s.data(), s.size());
  }
  return result;
}

std::string GetHeadKeyFor(const CommitId& head) {
  return Concatenate({kHeadPrefix, head});
}

std::string GetCommitKeyFor(const CommitId& commit_id) {
  return Concatenate({kCommitPrefix, commit_id});
}

std::string GetUnsyncedCommitKeyFor(const CommitId& commit_id) {
  return Concatenate({kUnsyncedCommitPrefix, commit_id});
}

std::string GetUnsyncedObjectKeyFor(ObjectIdView object_id) {
  return Concatenate({kUnsyncedObjectPrefix, object_id});
}

std::string GetImplicitJournalMetaKeyFor(const JournalId& journal_id) {
  return Concatenate({kImplicitJournalMetaPrefix, journal_id});
}

std::string GetJournalEntryPrefixFor(const JournalId& journal_id) {
  return Concatenate({kJournalPrefix, journal_id});
}

std::string GetJournalEntryKeyFor(const JournalId id, ftl::StringView key) {
  return Concatenate({kJournalPrefix, id, "/", key});
}

std::string GetJournalEntryValueFor(ftl::StringView value,
                                    KeyPriority priority) {
  char priority_byte =
      (priority == KeyPriority::EAGER) ? kJournalEagerEntry : kJournalLazyEntry;
  return Concatenate({{&kJournalEntryAdd, 1}, {&priority_byte, 1}, value});
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
    change_.reset(new EntryChange());

    static int journal_prefix_length =
        kJournalPrefix.size() + kJournalIdSize + 1;
    leveldb::Slice key_slice = it_->key();
    key_slice.remove_prefix(journal_prefix_length);
    change_->entry.key = key_slice.ToString();

    leveldb::Slice value = it_->value();
    if (value.data()[0] == kJournalEntryAdd) {
      change_->deleted = false;
      change_->entry.priority = (value.data()[1] == kJournalLazyEntry)
                                    ? KeyPriority::LAZY
                                    : KeyPriority::EAGER;

      value.remove_prefix(2);
      change_->entry.object_id = value.ToString();
    } else {
      change_->deleted = true;
    }
  }

  std::unique_ptr<leveldb::Iterator> it_;
  const std::string prefix_;

  std::unique_ptr<EntryChange> change_;
};

}  // namespace

DB::Batch::Batch(std::function<Status(bool)> callback)
    : callback_(callback), executed_(false) {}

DB::Batch::~Batch() {
  if (!executed_)
    callback_(false);
}

Status DB::Batch::Execute() {
  FTL_DCHECK(!executed_);
  executed_ = true;
  return callback_(true);
}

DB::DB(std::string db_path) : db_path_(db_path) {}

DB::~DB() {
  FTL_DCHECK(!batch_);
}

Status DB::Init() {
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

std::unique_ptr<DB::Batch> DB::StartBatch() {
  FTL_DCHECK(!batch_);
  batch_.reset(new leveldb::WriteBatch());
  return std::unique_ptr<Batch>(new Batch([this](bool execute) {
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
  }));
}

Status DB::GetHeads(std::vector<CommitId>* heads) {
  return GetByPrefix(convert::ToSlice(kHeadPrefix), heads);
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

Status DB::CreateJournal(JournalType journal_type,
                         const CommitId& base,
                         std::unique_ptr<Journal>* journal) {
  JournalId id = NewJournalId(journal_type);
  *journal = JournalDBImpl::Simple(this, id, base);
  if (journal_type == JournalType::IMPLICIT) {
    return Put(GetImplicitJournalMetaKeyFor(id), base);
  }
  return Status::OK;
}

Status DB::CreateMergeJournal(const CommitId& base,
                              const CommitId& other,
                              std::unique_ptr<Journal>* journal) {
  *journal = JournalDBImpl::Merge(this, NewJournalId(JournalType::EXPLICIT),
                                  base, other);
  return Status::OK;
}

Status DB::GetImplicitJournalIds(std::vector<JournalId>* journal_ids) {
  return GetByPrefix(convert::ToSlice(kImplicitJournalMetaPrefix), journal_ids);
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
  static std::string kExplicitJournalPrefix = Concatenate(
      {kJournalPrefix, ftl::StringView(&kImplicitJournalIdPrefix, 1)});
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
                           ftl::StringView key,
                           ftl::StringView value,
                           KeyPriority priority) {
  return Put(GetJournalEntryKeyFor(journal_id, key),
             GetJournalEntryValueFor(value, priority));
}

Status DB::RemoveJournalEntry(const JournalId& journal_id,
                              convert::ExtendedStringView key) {
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
  return GetByPrefix(convert::ToSlice(kUnsyncedCommitPrefix), commit_ids);
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
  if (s == Status::INTERNAL_IO_ERROR) {
    return s;
  }
  *is_synced = (s == Status::NOT_FOUND);
  return Status::OK;
}

Status DB::GetUnsyncedObjectIds(std::vector<ObjectId>* object_ids) {
  return GetByPrefix(convert::ToSlice(kUnsyncedObjectPrefix), object_ids);
}

Status DB::MarkObjectIdSynced(ObjectIdView object_id) {
  return Delete(GetUnsyncedObjectKeyFor(object_id));
}

Status DB::MarkObjectIdUnsynced(ObjectIdView object_id) {
  return Put(GetUnsyncedObjectKeyFor(object_id), "");
}

Status DB::IsObjectSynced(ObjectIdView object_id, bool* is_synced) {
  std::string value;
  Status s = Get(GetUnsyncedObjectKeyFor(object_id), &value);
  if (s == Status::INTERNAL_IO_ERROR) {
    return s;
  }
  *is_synced = (s == Status::NOT_FOUND);
  return Status::OK;
}

Status DB::GetByPrefix(const leveldb::Slice& prefix,
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
    return Status::INTERNAL_IO_ERROR;
  }
  key_suffixes->swap(result);
  return Status::OK;
}

Status DB::DeleteByPrefix(const leveldb::Slice& prefix) {
  std::unique_ptr<leveldb::Iterator> it(db_->NewIterator(read_options_));
  for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix);
       it->Next()) {
    Delete(it->key());
  }
  return it->status().ok() ? Status::OK : Status::INTERNAL_IO_ERROR;
}

Status DB::Get(convert::ExtendedStringView key, std::string* value) {
  leveldb::Status s = db_->Get(read_options_, key, value);
  if (s.IsNotFound()) {
    return Status::NOT_FOUND;
  }
  if (!s.ok()) {
    return Status::INTERNAL_IO_ERROR;
  }
  return Status::OK;
}

Status DB::Put(convert::ExtendedStringView key, ftl::StringView value) {
  if (batch_) {
    batch_->Put(key, convert::ToSlice(value));
    return Status::OK;
  }
  leveldb::Status s = db_->Put(write_options_, key, convert::ToSlice(value));
  return s.ok() ? Status::OK : Status::INTERNAL_IO_ERROR;
}

Status DB::Delete(convert::ExtendedStringView key) {
  if (batch_) {
    batch_->Delete(key);
    return Status::OK;
  }
  leveldb::Status s = db_->Delete(write_options_, key);
  return s.ok() ? Status::OK : Status::INTERNAL_IO_ERROR;
}

}  // namespace storage
