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

#include "apps/ledger/src/glue/crypto/hash.h"
#include "apps/ledger/src/storage/impl/btree/btree_utils.h"
#include "apps/ledger/src/storage/impl/commit_impl.h"
#include "apps/ledger/src/storage/impl/object_impl.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"

namespace storage {

namespace {

const char kLevelDbDir[] = "/leveldb";
const char kObjectDir[] = "/objects";
const char kStagingDir[] = "/staging";

const char kHexDigits[] = "0123456789ABCDEF";

const size_t kDefaultNodeSize = 1024u;

std::string ToHex(convert::ExtendedStringView string) {
  std::string result;
  result.reserve(string.size() * 2);
  for (unsigned char c : string) {
    result.push_back(kHexDigits[c >> 4]);
    result.push_back(kHexDigits[c & 0xf]);
  }
  return result;
}

Status StagingToDestination(size_t expected_size,
                            std::string source_path,
                            std::string destination_path) {
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

}  // namespace

class PageStorageImpl::FileWriter : public mtl::DataPipeDrainer::Client {
 public:
  FileWriter(const std::string& staging_dir, const std::string& object_dir)
      : staging_dir_(staging_dir),
        object_dir_(object_dir),
        drainer_(this),
        expected_size_(0),
        size_(0u) {}

  ~FileWriter() override {
    // Cleanup staging file.
    if (!file_path_.empty()) {
      fd_.reset();
      unlink(file_path_.c_str());
    }
  }

  void Start(mx::datapipe_consumer source,
             int64_t expected_size,
             std::function<void(Status, ObjectId)> callback) {
    expected_size_ = expected_size;
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
    drainer_.Start(std::move(source));
  }

  void OnDataAvailable(const void* data, size_t num_bytes) override {
    size_ += num_bytes;
    hash_.Update(data, num_bytes);
    if (!ftl::WriteFileDescriptor(fd_.get(), static_cast<const char*>(data),
                                  num_bytes)) {
      FTL_LOG(ERROR) << "Error writing data to disk: " << strerror(errno);
      callback_(Status::INTERNAL_IO_ERROR, "");
      return;
    }
  }

  void OnDataComplete() override {
    if (fsync(fd_.get()) != 0) {
      FTL_LOG(ERROR) << "Unable to save to disk.";
      callback_(Status::INTERNAL_IO_ERROR, "");
      return;
    }
    fd_.reset();
    if (expected_size_ >= 0 && size_ != static_cast<size_t>(expected_size_)) {
      FTL_LOG(ERROR) << "Received incorrect number of bytes. Expected: "
                     << expected_size_ << ", but received: " << size_;
      callback_(Status::IO_ERROR, "");
      return;
    }

    std::string object_id;
    hash_.Finish(&object_id);

    std::string final_path = object_dir_ + "/" + ToHex(object_id);
    Status status =
        StagingToDestination(size_, file_path_, std::move(final_path));
    if (status != Status::OK) {
      callback_(Status::INTERNAL_IO_ERROR, "");
      return;
    }

    callback_(Status::OK, std::move(object_id));
  }

 private:
  const std::string& staging_dir_;
  const std::string& object_dir_;
  std::function<void(Status, const ObjectId&)> callback_;
  mtl::DataPipeDrainer drainer_;
  std::string file_path_;
  ftl::UniqueFD fd_;
  glue::SHA256StreamingHash hash_;
  int64_t expected_size_;
  uint64_t size_;
};

PageStorageImpl::PageStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                 std::string page_dir,
                                 PageIdView page_id)
    : task_runner_(task_runner),
      page_dir_(page_dir),
      page_id_(page_id.ToString()),
      db_(this, page_dir_ + kLevelDbDir),
      objects_dir_(page_dir_ + kObjectDir),
      staging_dir_(page_dir_ + kStagingDir),
      page_sync_(nullptr) {}

PageStorageImpl::~PageStorageImpl() {}

Status PageStorageImpl::Init() {
  // Initialize DB.
  Status s = db_.Init();
  if (s != Status::OK) {
    return s;
  }

  // Initialize paths.
  if (!files::CreateDirectory(objects_dir_) ||
      !files::CreateDirectory(staging_dir_)) {
    FTL_LOG(ERROR) << "Unable to create directories for PageStorageImpl.";
    return Status::INTERNAL_IO_ERROR;
  }

  // Add the default page head if this page is empty.
  std::vector<CommitId> heads;
  s = db_.GetHeads(&heads);
  if (s != Status::OK) {
    return s;
  }
  if (heads.empty()) {
    s = db_.AddHead(std::string(kFirstPageCommitId, kCommitIdSize));
    if (s != Status::OK) {
      return s;
    }
  }

  // TODO(nellyv): The pages node size should be shared across devices.
  db_.SetNodeSize(kDefaultNodeSize);

  // Remove uncommited explicit journals.
  db_.RemoveExplicitJournals();

  // Commit uncommited implicit journals.
  std::vector<JournalId> journal_ids;
  s = db_.GetImplicitJournalIds(&journal_ids);
  if (s != Status::OK) {
    return s;
  }
  for (JournalId& id : journal_ids) {
    std::unique_ptr<Journal> journal;
    db_.GetImplicitJournal(id, &journal);
    bool async_test = false;
    journal->Commit([&s, &async_test](Status status, const CommitId&) {
      s = status;
      async_test = true;
    });
    FTL_DCHECK(async_test);
    if (s != Status::OK) {
      journal->Rollback();
      return s;
    }
  }

  return Status::OK;
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

Status PageStorageImpl::GetCommit(const CommitId& commit_id,
                                  std::unique_ptr<const Commit>* commit) {
  if (IsFirstCommit(commit_id)) {
    *commit = CommitImpl::Empty(this);
    return Status::OK;
  }
  std::string bytes;
  Status s = db_.GetCommitStorageBytes(commit_id, &bytes);
  if (s != Status::OK) {
    return s;
  }
  std::unique_ptr<const Commit> c =
      CommitImpl::FromStorageBytes(this, commit_id, std::move(bytes));
  if (!c) {
    return Status::FORMAT_ERROR;
  }
  commit->swap(c);
  return Status::OK;
}

void PageStorageImpl::AddCommitFromLocal(std::unique_ptr<Commit> commit,
                                         std::function<void(Status)> callback) {
  AddCommit(std::move(commit), ChangeSource::LOCAL, callback);
}

void PageStorageImpl::AddCommitFromSync(const CommitId& id,
                                        std::string storage_bytes,
                                        std::function<void(Status)> callback) {
  if (ContainsCommit(id) == Status::OK) {
    // The commit is already downloaded.
    callback(Status::OK);
    return;
  }

  std::unique_ptr<const Commit> commit =
      CommitImpl::FromStorageBytes(this, id, std::move(storage_bytes));
  if (!commit) {
    callback(Status::FORMAT_ERROR);
    return;
  }
  // Get all objects from sync and then add the commit object.
  btree::GetObjectsFromSync(
      commit->GetRootId(), this, ftl::MakeCopyable([
        this, commit = std::move(commit), callback = std::move(callback)
      ](Status s) mutable {
        AddCommit(std::move(commit), ChangeSource::SYNC, std::move(callback));
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

Status PageStorageImpl::GetUnsyncedCommits(
    std::vector<std::unique_ptr<const Commit>>* commits) {
  std::vector<CommitId> result_ids;
  Status s = db_.GetUnsyncedCommitIds(&result_ids);
  if (s != Status::OK) {
    return s;
  }

  std::vector<std::unique_ptr<const Commit>> result;
  for (size_t i = 0; i < result_ids.size(); ++i) {
    std::unique_ptr<const Commit> commit;
    Status s = GetCommit(result_ids[i], &commit);
    if (s != Status::OK) {
      return s;
    }
    result.push_back(std::move(commit));
  }

  commits->swap(result);
  return Status::OK;
}

Status PageStorageImpl::MarkCommitSynced(const CommitId& commit_id) {
  return db_.MarkCommitIdSynced(commit_id);
}

Status PageStorageImpl::GetDeltaObjects(const CommitId& commit_id,
                                        std::vector<ObjectId>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetUnsyncedObjects(const CommitId& commit_id,
                                           std::vector<ObjectId>* objects) {
  std::vector<ObjectId> result;
  std::set<ObjectId> commit_objects;
  std::unique_ptr<const Commit> commit;
  Status s = GetCommit(commit_id, &commit);
  if (s != Status::OK) {
    return s;
  }
  s = btree::GetObjects(commit->GetRootId(), this, &commit_objects);
  if (s != Status::OK) {
    return s;
  }
  std::vector<ObjectId> unsynced_objects;
  s = db_.GetUnsyncedObjectIds(&unsynced_objects);
  if (s != Status::OK) {
    return s;
  }

  std::set_intersection(commit_objects.begin(), commit_objects.end(),
                        unsynced_objects.begin(), unsynced_objects.end(),
                        std::back_inserter(result));
  objects->swap(result);
  return Status::OK;
}

Status PageStorageImpl::MarkObjectSynced(ObjectIdView object_id) {
  return db_.MarkObjectIdSynced(object_id);
}

void PageStorageImpl::AddObjectFromSync(
    ObjectIdView object_id,
    mx::datapipe_consumer data,
    size_t size,
    const std::function<void(Status)>& callback) {
  AddObject(std::move(data), size,
            [ this, object_id = object_id.ToString(), callback ](
                Status status, ObjectId found_id) {
              if (status != Status::OK) {
                callback(status);
              } else if (found_id != object_id) {
                FTL_LOG(ERROR) << "Object ID mismatch. Given ID: " << object_id
                               << ". Found: " << found_id;
                files::DeletePath(GetFilePath(found_id), false);
                callback(Status::OBJECT_ID_MISMATCH);
              } else {
                callback(Status::OK);
              }
            });
}

void PageStorageImpl::AddObjectFromLocal(
    mx::datapipe_consumer data,
    int64_t size,
    const std::function<void(Status, ObjectId)>& callback) {
  AddObject(std::move(data), size, [ this, callback = std::move(callback) ](
                                       Status status, ObjectId object_id) {
    untracked_objects_.insert(object_id);
    callback(status, std::move(object_id));
  });
}

void PageStorageImpl::GetObject(
    ObjectIdView object_id,
    const std::function<void(Status, std::unique_ptr<const Object>)>&
        callback) {
  std::string file_path = GetFilePath(object_id);
  if (!files::IsFile(file_path)) {
    GetObjectFromSync(object_id, callback);
    return;
  }

  task_runner_->PostTask([
    callback, object_id = convert::ToString(object_id),
    file_path = std::move(file_path)
  ]() mutable {
    callback(Status::OK, std::make_unique<ObjectImpl>(std::move(object_id),
                                                      std::move(file_path)));
  });
}

Status PageStorageImpl::GetObjectSynchronous(
    ObjectIdView object_id,
    std::unique_ptr<const Object>* object) {
  std::string file_path = GetFilePath(object_id);
  if (!files::IsFile(file_path))
    return Status::NOT_FOUND;

  *object =
      std::make_unique<ObjectImpl>(object_id.ToString(), std::move(file_path));
  return Status::OK;
}

Status PageStorageImpl::AddObjectSynchronous(
    convert::ExtendedStringView data,
    std::unique_ptr<const Object>* object) {
  ObjectId object_id = glue::SHA256Hash(data.data(), data.size());

  // Using mkstemp to create an unique file. XXXXXX will be replaced.
  std::string staging_path = staging_dir_ + "/XXXXXX";
  ftl::UniqueFD fd(mkstemp(&staging_path[0]));
  if (!ftl::WriteFileDescriptor(fd.get(), data.data(), data.size()))
    return Status::INTERNAL_IO_ERROR;
  if (fsync(fd.get()) != 0)
    return Status::INTERNAL_IO_ERROR;
  fd.reset();
  Status status =
      StagingToDestination(data.size(), staging_path, GetFilePath(object_id));
  if (status != Status::OK)
    return status;
  return GetObjectSynchronous(object_id, object);
}

Status PageStorageImpl::SetSyncMetadata(ftl::StringView sync_state) {
  return db_.SetSyncMetadata(sync_state);
}

Status PageStorageImpl::GetSyncMetadata(std::string* sync_state) {
  return db_.GetSyncMetadata(sync_state);
}

void PageStorageImpl::NotifyWatchers(const Commit& commit,
                                     ChangeSource source) {
  for (CommitWatcher* watcher : watchers_) {
    watcher->OnNewCommit(commit, source);
  }
}

void PageStorageImpl::AddCommit(std::unique_ptr<const Commit> commit,
                                ChangeSource source,
                                std::function<void(Status)> callback) {
  // Apply all changes atomically.
  std::unique_ptr<DB::Batch> batch = db_.StartBatch();
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
  s = db_.AddHead(commit->GetId());
  if (s != Status::OK) {
    callback(s);
    return;
  }

  // Commits must arrive in order: Check that the parents are stored in DB and
  // remove them from the heads if they are present.
  for (const CommitId& parent_id : commit->GetParentIds()) {
    s = ContainsCommit(parent_id);
    if (s != Status::OK) {
      FTL_LOG(ERROR) << "Failed to find parent commit \"" << parent_id
                     << "\" of commit \"" << commit->GetId() << "\"";
      if (s == Status::NOT_FOUND) {
        callback(Status::ILLEGAL_STATE);
      } else {
        callback(Status::INTERNAL_IO_ERROR);
      }
      return;
    }
    db_.RemoveHead(parent_id);
  }

  s = batch->Execute();
  if (s != Status::OK) {
    callback(s);
    return;
  }

  callback(Status::OK);
  NotifyWatchers(*(commit.get()), source);
}

Status PageStorageImpl::ContainsCommit(const CommitId& id) {
  if (IsFirstCommit(id)) {
    return Status::OK;
  }
  std::string bytes;
  return db_.GetCommitStorageBytes(id, &bytes);
}

bool PageStorageImpl::IsFirstCommit(const CommitId& id) {
  static std::string first_commit_id =
      std::string(kFirstPageCommitId, kCommitIdSize);
  return id == first_commit_id;
}

void PageStorageImpl::AddObject(
    mx::datapipe_consumer data,
    int64_t size,
    const std::function<void(Status, ObjectId)>& callback) {
  auto file_writer = std::make_unique<FileWriter>(staging_dir_, objects_dir_);
  FileWriter* file_writer_ptr = file_writer.get();
  writers_.push_back(std::move(file_writer));

  auto cleanup = [this, file_writer_ptr]() {
    auto writer_it =
        std::find_if(writers_.begin(), writers_.end(),
                     [file_writer_ptr](const std::unique_ptr<FileWriter>& c) {
                       return c.get() == file_writer_ptr;
                     });
    FTL_DCHECK(writer_it != writers_.end());
    writers_.erase(writer_it);
  };

  file_writer_ptr->Start(std::move(data), size, [
    this, cleanup = std::move(cleanup), callback = std::move(callback)
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
  ](Status status, uint64_t size, mx::datapipe_consumer data) {
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }
    AddObjectFromSync(object_id, std::move(data), size, [
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

std::string PageStorageImpl::GetFilePath(ObjectIdView object_id) {
  return objects_dir_ + "/" + ToHex(object_id);
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
