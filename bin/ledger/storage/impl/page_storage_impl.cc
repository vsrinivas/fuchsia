// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/page_storage_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

#include "apps/ledger/src/callback/asynchronous_callback.h"
#include "apps/ledger/src/callback/trace_callback.h"
#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/storage/impl/btree/diff.h"
#include "apps/ledger/src/storage/impl/btree/iterator.h"
#include "apps/ledger/src/storage/impl/commit_impl.h"
#include "apps/ledger/src/storage/impl/constants.h"
#include "apps/ledger/src/storage/impl/inlined_object_impl.h"
#include "apps/ledger/src/storage/impl/journal_db_impl.h"
#include "apps/ledger/src/storage/impl/object_impl.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/strings/concatenate.h"

namespace storage {

namespace {

using StreamingHash = glue::SHA256StreamingHash;

const char kLevelDbDir[] = "/leveldb";
const char kObjectDir[] = "/objects";
const char kStagingDir[] = "/staging";

static_assert(kObjectHashSize == StreamingHash::kHashSize,
              "Unexpected kObjectHashSize value");

struct StringPointerComparator {
  using is_transparent = std::true_type;

  bool operator()(const std::string* str1, const std::string* str2) const {
    return *str1 < *str2;
  }

  bool operator()(const std::string* str1, const CommitIdView* str2) const {
    return *str1 < *str2;
  }

  bool operator()(const CommitIdView* str1, const std::string* str2) const {
    return *str1 < *str2;
  }
};

std::string GetFilePath(ftl::StringView objects_dir,
                        convert::ExtendedStringView object_id) {
  std::string hex = convert::ToHex(object_id);

  FTL_DCHECK(hex.size() > 2);
  ftl::StringView hex_view = hex;
  return ftl::Concatenate(
      {objects_dir, "/", hex_view.substr(0, 2), "/", hex_view.substr(2)});
}

Status StagingToDestination(size_t expected_size,
                            std::string source_path,
                            std::string destination_path) {
  TRACE_DURATION("ledger", "page_storage_staging_to_destination");
  // Check if file already exists.
  size_t size = 0;
  if (files::GetFileSize(destination_path, &size)) {
    if (size != expected_size) {
      // If size is not the correct one, something is really wrong.
      FTL_LOG(ERROR) << "Internal error. Path \"" << destination_path
                     << "\" has wrong size. Expected: " << expected_size
                     << ", but found: " << size;

      return Status::INTERNAL_IO_ERROR;
    }
    // Source path already existed at destination. Clear the source.
    files::DeletePath(source_path, false);
    return Status::OK;
  }

  std::string destination_dir = files::GetDirectoryName(destination_path);
  if (!files::IsDirectory(destination_dir) &&
      !files::CreateDirectory(destination_dir)) {
    return Status::INTERNAL_IO_ERROR;
  }

  if (rename(source_path.c_str(), destination_path.c_str()) != 0) {
    // If rename failed, the file might have been saved by another call.
    if (!files::GetFileSize(destination_path, &size) || size != expected_size) {
      FTL_LOG(ERROR) << "Internal error. Failed to rename \n\"" << source_path
                     << "\" to \n\"" << destination_path << "\"";
      return Status::INTERNAL_IO_ERROR;
    }
    // Source path already existed at destination. Clear the source.
    files::DeletePath(source_path, false);
  }
  return Status::OK;
}

class FileWriterOnIOThread {
 public:
  FileWriterOnIOThread(const std::string& staging_dir,
                       const std::string& object_dir)
      : staging_dir_(staging_dir), object_dir_(object_dir) {}

  ~FileWriterOnIOThread() {
    // Cleanup staging file.
    if (!file_path_.empty()) {
      fd_.reset();
      unlink(file_path_.c_str());
    }
  }

  void Start(std::unique_ptr<DataSource> data_source,
             std::function<void(Status, ObjectId)> callback) {
    callback_ = std::move(callback);
    // Using mkstemp to create an unique file. XXXXXX will be replaced.
    file_path_ = staging_dir_ + "/XXXXXX";
    fd_.reset(mkstemp(&file_path_[0]));
    if (!fd_.is_valid()) {
      FTL_LOG(ERROR) << "Unable to create file in staging directory ("
                     << staging_dir_ << ")";
      callback_(Status::INTERNAL_IO_ERROR, "");
      return;
    }
    data_source_ = std::move(data_source);
    data_source_->Get([this](std::unique_ptr<DataSource::DataChunk> chunk,
                             DataSource::Status status) {
      if (status == DataSource::Status::ERROR) {
        callback_(Status::IO_ERROR, "");
        return;
      }
      OnDataAvailable(chunk->Get());
      if (status == DataSource::Status::DONE) {
        OnDataComplete();
      }
    });
  }

 private:
  void OnDataAvailable(ftl::StringView data) {
    hash_.Update(data);
    if (!ftl::WriteFileDescriptor(fd_.get(), data.data(), data.size())) {
      FTL_LOG(ERROR) << "Error writing data to disk: " << strerror(errno);
      callback_(Status::INTERNAL_IO_ERROR, "");
      return;
    }
  }

  void OnDataComplete() {
    if (fsync(fd_.get()) != 0) {
      FTL_LOG(ERROR) << "Unable to save to disk.";
      callback_(Status::INTERNAL_IO_ERROR, "");
      return;
    }
    fd_.reset();

    std::string object_id;
    hash_.Finish(&object_id);

    std::string final_path = storage::GetFilePath(object_dir_, object_id);
    Status status = StagingToDestination(data_source_->GetSize(), file_path_,
                                         std::move(final_path));
    if (status != Status::OK) {
      callback_(Status::INTERNAL_IO_ERROR, "");
      return;
    }

    callback_(Status::OK, std::move(object_id));
  }

  const std::string& staging_dir_;
  const std::string& object_dir_;
  std::function<void(Status, ObjectId)> callback_;
  std::unique_ptr<DataSource> data_source_;
  std::string file_path_;
  ftl::UniqueFD fd_;
  StreamingHash hash_;
};

// Drains a data source into a storage object.
class ObjectSourceHandler {
 public:
  ObjectSourceHandler() {}
  virtual ~ObjectSourceHandler() {}

  virtual void Start(std::function<void(Status, ObjectId)> callback) = 0;

  static std::unique_ptr<ObjectSourceHandler> Create(
      std::unique_ptr<DataSource> data_source,
      ftl::RefPtr<ftl::TaskRunner> main_runner,
      ftl::RefPtr<ftl::TaskRunner> io_runner,
      const std::string& staging_dir,
      const std::string& object_dir);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ObjectSourceHandler);
};

class SmallObjectObjectSourceHandler : public ObjectSourceHandler {
 public:
  SmallObjectObjectSourceHandler(std::unique_ptr<DataSource> data_source)
      : data_source_(std::move(data_source)) {}

  void Start(std::function<void(Status, ObjectId)> callback) override {
    callback_ = std::move(callback);

    data_source_->Get([this](std::unique_ptr<DataSource::DataChunk> chunk,
                             DataSource::Status status) {
      if (status == DataSource::Status::ERROR) {
        callback_(Status::IO_ERROR, "");
        return;
      }
      auto view = chunk->Get();
      content_.append(view.data(), view.size());
      if (status == DataSource::Status::DONE) {
        callback_(Status::OK, std::move(content_));
      }
    });
  }

 private:
  std::unique_ptr<DataSource> data_source_;
  std::string content_;
  std::function<void(Status, ObjectId)> callback_;
};

class FileWriter : public ObjectSourceHandler {
 public:
  FileWriter(std::unique_ptr<DataSource> data_source,
             ftl::RefPtr<ftl::TaskRunner> main_runner,
             ftl::RefPtr<ftl::TaskRunner> io_runner,
             const std::string& staging_dir,
             const std::string& object_dir)
      : data_source_(std::move(data_source)),
        main_runner_(std::move(main_runner)),
        io_runner_(std::move(io_runner)),
        file_writer_on_io_thread_(
            std::make_unique<FileWriterOnIOThread>(staging_dir, object_dir)),
        weak_ptr_factory_(this) {
    FTL_DCHECK(main_runner_->RunsTasksOnCurrentThread());
  }

  ~FileWriter() {
    FTL_DCHECK(main_runner_->RunsTasksOnCurrentThread());

    if (!io_runner_->RunsTasksOnCurrentThread()) {
      io_runner_->PostTask(ftl::MakeCopyable([
        this,
        guard = std::make_unique<std::lock_guard<std::mutex>>(deletion_mutex_)
      ] { file_writer_on_io_thread_.reset(); }));
      std::lock_guard<std::mutex> wait_for_deletion(deletion_mutex_);
    }
  }

  void Start(std::function<void(Status, ObjectId)> callback) override {
    FTL_DCHECK(main_runner_->RunsTasksOnCurrentThread());

    if (io_runner_->RunsTasksOnCurrentThread()) {
      file_writer_on_io_thread_->Start(std::move(data_source_),
                                       std::move(callback));
      return;
    }
    callback_ = std::move(callback);
    io_runner_->PostTask(ftl::MakeCopyable(
        [ this, weak_this = weak_ptr_factory_.GetWeakPtr() ]() mutable {
          // Called on the io runner.

          // |this| cannot be deleted here, because if the destructor of
          // FileWriter has been called after Start and before this has been
          // run, it is still waiting on the lock to be released as the posts
          // are run in-order.
          file_writer_on_io_thread_->Start(std::move(data_source_), [
            weak_this, main_runner = main_runner_
          ](Status status, ObjectId object_id) {
            // Called on the io runner.

            main_runner->PostTask(
                [ weak_this, status, object_id = std::move(object_id) ]() {
                  // Called on the main runner.

                  if (weak_this) {
                    weak_this->callback_(status, std::move(object_id));
                  }
                });
          });
        }));
  }

 private:
  std::mutex deletion_mutex_;
  std::unique_ptr<DataSource> data_source_;
  ftl::RefPtr<ftl::TaskRunner> main_runner_;
  ftl::RefPtr<ftl::TaskRunner> io_runner_;

  std::function<void(Status, ObjectId)> callback_;

  std::unique_ptr<FileWriterOnIOThread> file_writer_on_io_thread_;

  ftl::WeakPtrFactory<FileWriter> weak_ptr_factory_;
};

std::unique_ptr<ObjectSourceHandler> ObjectSourceHandler::Create(
    std::unique_ptr<DataSource> data_source,
    ftl::RefPtr<ftl::TaskRunner> main_runner,
    ftl::RefPtr<ftl::TaskRunner> io_runner,
    const std::string& staging_dir,
    const std::string& object_dir) {
  if (data_source->GetSize() < kObjectHashSize) {
    return std::make_unique<SmallObjectObjectSourceHandler>(
        std::move(data_source));
  }
  return std::make_unique<FileWriter>(
      std::move(data_source), std::move(main_runner), std::move(io_runner),
      staging_dir, object_dir);
}

Status RollbackJournalInternal(std::unique_ptr<Journal> journal) {
  return static_cast<JournalDBImpl*>(journal.get())->Rollback();
}

}  // namespace

PageStorageImpl::PageStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                 ftl::RefPtr<ftl::TaskRunner> io_runner,
                                 coroutine::CoroutineService* coroutine_service,
                                 std::string page_dir,
                                 PageId page_id)
    : main_runner_(task_runner),
      io_runner_(io_runner),
      coroutine_service_(coroutine_service),
      page_dir_(page_dir),
      page_id_(std::move(page_id)),
      db_(coroutine_service, this, page_dir_ + kLevelDbDir),
      objects_dir_(page_dir_ + kObjectDir),
      staging_dir_(page_dir_ + kStagingDir),
      page_sync_(nullptr) {}

PageStorageImpl::~PageStorageImpl() {}

void PageStorageImpl::Init(std::function<void(Status)> callback) {
  // Initialize DB.
  Status s = db_.Init();
  if (s != Status::OK) {
    callback(s);
    return;
  }

  // Initialize paths.
  if (!files::CreateDirectory(objects_dir_) ||
      !files::CreateDirectory(staging_dir_)) {
    FTL_LOG(ERROR) << "Unable to create directories for PageStorageImpl.";
    callback(Status::INTERNAL_IO_ERROR);
    return;
  }

  // Add the default page head if this page is empty.
  std::vector<CommitId> heads;
  s = db_.GetHeads(&heads);
  if (s != Status::OK) {
    callback(s);
    return;
  }
  if (heads.empty()) {
    s = db_.AddHead(kFirstPageCommitId, 0);
    if (s != Status::OK) {
      callback(s);
      return;
    }
  }

  // Remove uncommited explicit journals.
  db_.RemoveExplicitJournals();

  // Commit uncommited implicit journals.
  std::vector<JournalId> journal_ids;
  s = db_.GetImplicitJournalIds(&journal_ids);
  if (s != Status::OK) {
    callback(s);
    return;
  }
  auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
  for (JournalId& id : journal_ids) {
    std::unique_ptr<Journal> journal;
    s = db_.GetImplicitJournal(id, &journal);
    if (s != Status::OK) {
      FTL_LOG(ERROR) << "Failed to get implicit journal with status " << s
                     << ". journal id: " << id;
      callback(s);
      return;
    }

    CommitJournal(
        std::move(journal), [status_callback = waiter->NewCallback()](
                                Status status, std::unique_ptr<const Commit>) {
          if (status != Status::OK) {
            FTL_LOG(ERROR) << "Failed to commit implicit journal created in "
                              "previous Ledger execution.";
          }
          status_callback(status);
        });
  }

  waiter->Finalize(std::move(callback));
}

PageId PageStorageImpl::GetId() {
  return page_id_;
}

void PageStorageImpl::SetSyncDelegate(PageSyncDelegate* page_sync) {
  page_sync_ = page_sync;
}

Status PageStorageImpl::GetHeadCommitIds(std::vector<CommitId>* commit_ids) {
  return db_.GetHeads(commit_ids);
}

void PageStorageImpl::GetCommit(
    CommitIdView commit_id,
    std::function<void(Status, std::unique_ptr<const Commit>)> callback) {
  if (IsFirstCommit(commit_id)) {
    CommitImpl::Empty(this, std::move(callback));
    return;
  }
  std::string bytes;
  Status s = db_.GetCommitStorageBytes(commit_id, &bytes);
  if (s != Status::OK) {
    callback(s, nullptr);
    return;
  }
  std::unique_ptr<const Commit> commit = CommitImpl::FromStorageBytes(
      this, commit_id.ToString(), std::move(bytes));
  if (!commit) {
    callback(Status::FORMAT_ERROR, nullptr);
    return;
  }
  callback(Status::OK, std::move(commit));
}

void PageStorageImpl::AddCommitFromLocal(std::unique_ptr<const Commit> commit,
                                         std::function<void(Status)> callback) {
  // If the commit is already present, do nothing.
  if (ContainsCommit(commit->GetId()) == Status::OK) {
    callback(Status::OK);
    return;
  }
  std::vector<std::unique_ptr<const Commit>> commits;
  commits.reserve(1);
  commits.push_back(std::move(commit));
  AddCommits(std::move(commits), ChangeSource::LOCAL, callback);
}

void PageStorageImpl::AddCommitsFromSync(
    std::vector<CommitIdAndBytes> ids_and_bytes,
    std::function<void(Status)> callback) {
  std::vector<std::unique_ptr<const Commit>> commits;

  std::map<const CommitId*, const Commit*, StringPointerComparator> leaves;
  commits.reserve(ids_and_bytes.size());

  for (auto& id_and_bytes : ids_and_bytes) {
    ObjectId id = std::move(id_and_bytes.id);
    std::string storage_bytes = std::move(id_and_bytes.bytes);
    if (ContainsCommit(id) == Status::OK) {
      MarkCommitSynced(id);
      continue;
    }

    std::unique_ptr<const Commit> commit =
        CommitImpl::FromStorageBytes(this, id, std::move(storage_bytes));
    if (!commit) {
      FTL_LOG(ERROR) << "Unable to add commit. Id: " << convert::ToHex(id);
      callback(Status::FORMAT_ERROR);
      return;
    }

    // Remove parents from leaves.
    for (const auto& parent_id : commit->GetParentIds()) {
      auto it = leaves.find(&parent_id);
      if (it != leaves.end()) {
        leaves.erase(it);
      }
    }
    leaves[&commit->GetId()] = commit.get();
    commits.push_back(std::move(commit));
  }

  if (commits.empty()) {
    callback(Status::OK);
    return;
  }

  auto waiter = callback::StatusWaiter<Status>::Create(Status::OK);
  // Get all objects from sync and then add the commit objects.
  for (const auto& leaf : leaves) {
    btree::GetObjectsFromSync(coroutine_service_, this,
                              leaf.second->GetRootId(), waiter->NewCallback());
  }

  waiter->Finalize(ftl::MakeCopyable([
    this, commits = std::move(commits), callback = std::move(callback)
  ](Status status) mutable {
    if (status != Status::OK) {
      callback(status);
      return;
    }

    AddCommits(std::move(commits), ChangeSource::SYNC, callback);
  }));
}

Status PageStorageImpl::StartCommit(const CommitId& commit_id,
                                    JournalType journal_type,
                                    std::unique_ptr<Journal>* journal) {
  return db_.CreateJournal(journal_type, commit_id, journal);
}

Status PageStorageImpl::StartMergeCommit(const CommitId& left,
                                         const CommitId& right,
                                         std::unique_ptr<Journal>* journal) {
  return db_.CreateMergeJournal(left, right, journal);
}

void PageStorageImpl::CommitJournal(
    std::unique_ptr<Journal> journal,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  JournalDBImpl* journal_ptr = static_cast<JournalDBImpl*>(journal.get());
  // |journal| will now be owned by the Commit callback, making sure that it is
  // not deleted before the end of the computation.
  journal_ptr->Commit(ftl::MakeCopyable([
    journal = std::move(journal), callback = std::move(callback)
  ](Status status, std::unique_ptr<const storage::Commit> commit) mutable {
    if (status != Status::OK) {
      // Commit failed, roll the journal back.
      RollbackJournalInternal(std::move(journal));
    }
    callback(status, std::move(commit));
  }));
}

Status PageStorageImpl::RollbackJournal(std::unique_ptr<Journal> journal) {
  return RollbackJournalInternal(std::move(journal));
}

Status PageStorageImpl::AddCommitWatcher(CommitWatcher* watcher) {
  watchers_.push_back(watcher);
  return Status::OK;
}

Status PageStorageImpl::RemoveCommitWatcher(CommitWatcher* watcher) {
  auto watcher_it =
      std::find_if(watchers_.begin(), watchers_.end(),
                   [watcher](CommitWatcher* w) { return w == watcher; });
  if (watcher_it == watchers_.end()) {
    return Status::NOT_FOUND;
  }
  watchers_.erase(watcher_it);
  return Status::OK;
}

void PageStorageImpl::GetUnsyncedCommits(
    std::function<void(Status, std::vector<std::unique_ptr<const Commit>>)>
        callback) {
  std::vector<CommitId> commit_ids;
  Status s = db_.GetUnsyncedCommitIds(&commit_ids);
  if (s != Status::OK) {
    callback(s, {});
    return;
  }

  auto waiter = callback::Waiter<Status, std::unique_ptr<const Commit>>::Create(
      Status::OK);
  for (size_t i = 0; i < commit_ids.size(); ++i) {
    GetCommit(commit_ids[i], waiter->NewCallback());
  }
  waiter->Finalize([callback = std::move(callback)](
      Status s, std::vector<std::unique_ptr<const Commit>> commits) {
    if (s != Status::OK) {
      callback(s, {});
      return;
    }
    callback(Status::OK, std::move(commits));
  });
}

Status PageStorageImpl::MarkCommitSynced(const CommitId& commit_id) {
  return db_.MarkCommitIdSynced(commit_id);
}

Status PageStorageImpl::GetDeltaObjects(const CommitId& commit_id,
                                        std::vector<ObjectId>* objects) {
  return Status::NOT_IMPLEMENTED;
}

void PageStorageImpl::GetAllUnsyncedObjectIds(
    std::function<void(Status, std::vector<ObjectId>)> callback) {
  std::vector<ObjectId> unsynced_object_ids;
  Status s = db_.GetUnsyncedObjectIds(&unsynced_object_ids);
  callback(s, unsynced_object_ids);
}

Status PageStorageImpl::MarkObjectSynced(ObjectIdView object_id) {
  return db_.MarkObjectIdSynced(object_id);
}

void PageStorageImpl::AddObjectFromLocal(
    std::unique_ptr<DataSource> data_source,
    const std::function<void(Status, ObjectId)>& callback) {
  AddObject(std::move(data_source), [ this, callback = std::move(callback) ](
                                        Status status, ObjectId object_id) {
    untracked_objects_.insert(object_id);
    callback(status, std::move(object_id));
  });
}

void PageStorageImpl::GetObject(
    ObjectIdView object_id,
    Location location,
    const std::function<void(Status, std::unique_ptr<const Object>)>&
        callback) {
  if (object_id.size() < kObjectHashSize) {
    callback(Status::OK,
             std::make_unique<InlinedObjectImpl>(object_id.ToString()));
    return;
  }
  std::string file_path = GetFilePath(object_id);
  if (!files::IsFile(file_path)) {
    if (location == Location::NETWORK) {
      GetObjectFromSync(object_id, callback);
    } else {
      callback(Status::NOT_FOUND, nullptr);
    }
    return;
  }
  callback(Status::OK, std::make_unique<ObjectImpl>(object_id.ToString(),
                                                    std::move(file_path)));
}

Status PageStorageImpl::SetSyncMetadata(ftl::StringView key,
                                        ftl::StringView value) {
  return db_.SetSyncMetadata(key, value);
}

Status PageStorageImpl::GetSyncMetadata(ftl::StringView key,
                                        std::string* value) {
  return db_.GetSyncMetadata(key, value);
}

void PageStorageImpl::GetCommitContents(const Commit& commit,
                                        std::string min_key,
                                        std::function<bool(Entry)> on_next,
                                        std::function<void(Status)> on_done) {
  btree::ForEachEntry(
      coroutine_service_, this, commit.GetRootId(), min_key,
      [on_next = std::move(on_next)](btree::EntryAndNodeId next) {
        return on_next(next.entry);
      },
      std::move(on_done));
}

void PageStorageImpl::GetEntryFromCommit(
    const Commit& commit,
    std::string key,
    std::function<void(Status, Entry)> callback) {
  std::unique_ptr<bool> key_found = std::make_unique<bool>(false);
  auto on_next = [ key, key_found = key_found.get(),
                   callback ](btree::EntryAndNodeId next) {
    if (next.entry.key == key) {
      *key_found = true;
      callback(Status::OK, next.entry);
    }
    return false;
  };

  auto on_done = ftl::MakeCopyable([
    key_found = std::move(key_found), callback = std::move(callback)
  ](Status s) mutable {
    if (*key_found) {
      return;
    }
    if (s == Status::OK) {
      callback(Status::NOT_FOUND, Entry());
      return;
    }
    callback(s, Entry());
  });
  btree::ForEachEntry(coroutine_service_, this, commit.GetRootId(),
                      std::move(key), std::move(on_next), std::move(on_done));
}

void PageStorageImpl::GetCommitContentsDiff(
    const Commit& base_commit,
    const Commit& other_commit,
    std::string min_key,
    std::function<bool(EntryChange)> on_next_diff,
    std::function<void(Status)> on_done) {
  btree::ForEachDiff(coroutine_service_, this, base_commit.GetRootId(),
                     other_commit.GetRootId(), std::move(min_key),
                     std::move(on_next_diff), std::move(on_done));
}

void PageStorageImpl::NotifyWatchers() {
  while (!commits_to_send_.empty()) {
    auto to_send = std::move(commits_to_send_.front());
    for (CommitWatcher* watcher : watchers_) {
      watcher->OnNewCommits(std::move(to_send.second), to_send.first);
    }
    commits_to_send_.pop();
  }
}

void PageStorageImpl::AddCommits(
    std::vector<std::unique_ptr<const Commit>> commits,
    ChangeSource source,
    std::function<void(Status)> callback) {
  // Apply all changes atomically.
  std::unique_ptr<DB::Batch> batch = db_.StartBatch();
  std::set<const CommitId*, StringPointerComparator> added_commits;

  for (const auto& commit : commits) {
    Status s =
        db_.AddCommitStorageBytes(commit->GetId(), commit->GetStorageBytes());
    if (s != Status::OK) {
      callback(s);
      return;
    }

    if (source == ChangeSource::LOCAL) {
      s = db_.MarkCommitIdUnsynced(commit->GetId(), commit->GetTimestamp());
      if (s != Status::OK) {
        callback(s);
        return;
      }
    }

    // Update heads.
    s = db_.AddHead(commit->GetId(), commit->GetTimestamp());
    if (s != Status::OK) {
      callback(s);
      return;
    }

    // Commits must arrive in order: Check that the parents are stored in DB and
    // remove them from the heads if they are present.
    for (const CommitIdView& parent_id : commit->GetParentIds()) {
      if (added_commits.count(&parent_id) == 0) {
        s = ContainsCommit(parent_id);
        if (s != Status::OK) {
          FTL_LOG(ERROR) << "Failed to find parent commit \""
                         << ToHex(parent_id) << "\" of commit \""
                         << convert::ToHex(commit->GetId()) << "\"";
          if (s == Status::NOT_FOUND) {
            callback(Status::ILLEGAL_STATE);
          } else {
            callback(Status::INTERNAL_IO_ERROR);
          }
          return;
        }
      }
      db_.RemoveHead(parent_id);
    }

    added_commits.insert(&commit->GetId());
  }

  Status s = batch->Execute();
  bool notify_watchers = commits_to_send_.empty();
  commits_to_send_.emplace(source, std::move(commits));
  callback(s);

  if (s == Status::OK && notify_watchers) {
    NotifyWatchers();
  }
}

Status PageStorageImpl::ContainsCommit(CommitIdView id) {
  if (IsFirstCommit(id)) {
    return Status::OK;
  }
  std::string bytes;
  return db_.GetCommitStorageBytes(id, &bytes);
}

bool PageStorageImpl::IsFirstCommit(CommitIdView id) {
  return id == kFirstPageCommitId;
}

void PageStorageImpl::AddObjectFromSync(ObjectIdView object_id,
                                        std::unique_ptr<DataSource> data_source,
                                        std::function<void(Status)> callback) {
  AddObject(std::move(data_source), [
    this, object_id = object_id.ToString(), callback = std::move(callback)
  ](Status status, ObjectId found_id) {
    if (status != Status::OK) {
      callback(status);
    } else if (found_id != object_id) {
      FTL_LOG(ERROR) << "Object ID mismatch. Given ID: "
                     << convert::ToHex(object_id)
                     << ". Found: " << convert::ToHex(found_id);
      files::DeletePath(GetFilePath(found_id), false);
      callback(Status::OBJECT_ID_MISMATCH);
    } else {
      callback(Status::OK);
    }
  });
}

void PageStorageImpl::AddObject(
    std::unique_ptr<DataSource> data_source,
    const std::function<void(Status, ObjectId)>& callback) {
  auto traced_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_storage_add_object");

  auto handler = pending_operation_manager_.Manage(
      ObjectSourceHandler::Create(std::move(data_source), main_runner_,
                                  io_runner_, staging_dir_, objects_dir_));

  (*handler.first)->Start([
    cleanup = std::move(handler.second), callback = std::move(traced_callback)
  ](Status status, ObjectId object_id) {
    callback(status, std::move(object_id));
    cleanup();
  });
}

void PageStorageImpl::GetObjectFromSync(
    ObjectIdView object_id,
    const std::function<void(Status, std::unique_ptr<const Object>)>&
        callback) {
  if (!page_sync_) {
    callback(Status::NOT_CONNECTED_ERROR, nullptr);
    return;
  }
  page_sync_->GetObject(object_id, [
    this, callback = std::move(callback), object_id = object_id.ToString()
  ](Status status, uint64_t size, mx::socket data) {
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }
    AddObjectFromSync(object_id, DataSource::Create(std::move(data), size), [
      this, callback = std::move(callback), object_id
    ](Status status) mutable {
      if (status != Status::OK) {
        callback(status, nullptr);
        return;
      }
      std::string file_path = GetFilePath(object_id);
      FTL_DCHECK(files::IsFile(file_path));
      callback(Status::OK, std::make_unique<ObjectImpl>(std::move(object_id),
                                                        std::move(file_path)));
    });
  });
}

std::string PageStorageImpl::GetFilePath(ObjectIdView object_id) const {
  return storage::GetFilePath(objects_dir_, object_id);
}

bool PageStorageImpl::ObjectIsUntracked(ObjectIdView object_id) {
  return untracked_objects_.find(object_id) != untracked_objects_.end();
}

void PageStorageImpl::MarkObjectTracked(ObjectIdView object_id) {
  auto it = untracked_objects_.find(object_id);
  if (it != untracked_objects_.end()) {
    untracked_objects_.erase(it);
  }
}

}  // namespace storage
