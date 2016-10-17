// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/page_storage_impl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "apps/ledger/glue/crypto/hash.h"
#include "apps/ledger/storage/impl/commit_impl.h"
#include "apps/ledger/storage/public/constants.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/mtl/data_pipe/data_pipe_drainer.h"

namespace storage {

namespace {

const char kLevelDbDir[] = "/leveldb";
const char kObjectDir[] = "/objects";
const char kStagingDir[] = "/staging";

const char kHexDigits[] = "0123456789ABCDEF";

std::string ToHex(const std::string& string) {
  std::string result;
  result.reserve(string.size() * 2);
  for (unsigned char c : string) {
    result.push_back(kHexDigits[c >> 4]);
    result.push_back(kHexDigits[c & 0xf]);
  }
  return result;
}

// TODO(qsr): Use the rename libc function when MG-329 is fixed.
int Rename(const char* src, const char* dst) {
  {
    ftl::UniqueFD src_fd(open(src, O_RDONLY));
    if (!src_fd.is_valid())
      return 1;
    ftl::UniqueFD dst_fd(open(dst, O_WRONLY | O_CREAT | O_EXCL));
    if (!dst_fd.is_valid())
      return 1;

    char buffer[4096];
    for (;;) {
      ssize_t read =
          ftl::ReadFileDescriptor(src_fd.get(), buffer, arraysize(buffer));
      if (read < 0)
        return 1;
      if (read == 0)
        break;
      if (!ftl::WriteFileDescriptor(dst_fd.get(), buffer, read))
        return 1;
    }
  }

  unlink(src);
  return 0;
}

}  // namespace

class PageStorageImpl::FileWriter : public mtl::DataPipeDrainer::Client {
 public:
  FileWriter(const std::string& staging_dir, const std::string& object_dir)
      : staging_dir_(staging_dir),
        object_dir_(object_dir),
        drainer_(this),
        expected_size_(0u),
        size_(0u) {}

  ~FileWriter() override {
    // Cleanup staging file.
    if (!file_path_.empty()) {
      fd_.reset();
      unlink(file_path_.c_str());
    }
  }

  void Start(mojo::ScopedDataPipeConsumerHandle source,
             uint64_t expected_size,
             std::function<void(Status, ObjectId)> callback) {
    expected_size_ = expected_size;
    callback_ = std::move(callback);
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
    fd_.reset();
    if (size_ != expected_size_) {
      FTL_LOG(ERROR) << "Received incorrect number of bytes. Expected: "
                     << expected_size_ << ", but received: " << size_;
      callback_(Status::IO_ERROR, "");
      return;
    }

    std::string object_id;
    hash_.Finish(&object_id);

    std::string final_path = object_dir_ + "/" + ToHex(object_id);

    // Check if file already exists.
    size_t size;
    if (files::GetFileSize(final_path, &size)) {
      if (size != expected_size_) {
        // If size is not the correct one, something is really wrong.
        FTL_LOG(ERROR) << "Internal error. Path \"" << final_path
                       << "\" has wrong size. Expected: " << expected_size_
                       << ", but found: " << size;

        callback_(Status::INTERNAL_IO_ERROR, "");
        return;
      }
    } else {
      if (Rename(file_path_.c_str(), final_path.c_str()) != 0) {
        // If rename failed, the file might have been saved by another call.
        if (!files::GetFileSize(final_path, &size) || size != expected_size_) {
          // If size is not the correct one, something is really wrong.
          FTL_LOG(ERROR) << "Internal error. Path \"" << final_path
                         << "\" has wrong size. Expected: " << expected_size_
                         << ", but found: " << size;
          callback_(Status::INTERNAL_IO_ERROR, "");
          return;
        }
      }
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
  uint64_t expected_size_;
  uint64_t size_;
};

PageStorageImpl::PageStorageImpl(std::string page_path, PageIdView page_id)
    : page_path_(page_path),
      page_id_(page_id.ToString()),
      db_(page_path_ + kLevelDbDir),
      objects_path_(page_path_ + kObjectDir),
      staging_path_(page_path_ + kStagingDir) {}

PageStorageImpl::~PageStorageImpl() {}

Status PageStorageImpl::Init() {
  // Initialize DB.
  Status s = db_.Init();
  if (s != Status::OK) {
    return s;
  }

  // Initialize paths.
  if (!files::CreateDirectory(objects_path_) ||
      !files::CreateDirectory(staging_path_)) {
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

  // Remove uncommited explicit journals.
  db_.RemoveExplicitJournals();
  // TODO(nellyv): Commit uncommited implicit journals.

  return Status::OK;
}

PageId PageStorageImpl::GetId() {
  return page_id_;
}

void PageStorageImpl::SetPageDeletionHandler(
    const std::function<void()>& on_page_deletion) {}

Status PageStorageImpl::GetHeadCommitIds(std::vector<CommitId>* commit_ids) {
  return db_.GetHeads(commit_ids);
}

Status PageStorageImpl::GetCommit(const CommitId& commit_id,
                                  std::unique_ptr<Commit>* commit) {
  std::string bytes;
  Status s = db_.GetCommitStorageBytes(commit_id, &bytes);
  if (s != Status::OK) {
    return s;
  }
  std::unique_ptr<Commit> c = CommitImpl::FromStorageBytes(commit_id, bytes);
  if (!c) {
    return Status::FORMAT_ERROR;
  }
  commit->swap(c);
  return Status::OK;
}

Status PageStorageImpl::AddCommitFromLocal(std::unique_ptr<Commit> commit) {
  return AddCommit(std::move(commit), ChangeSource::LOCAL);
}

Status PageStorageImpl::AddCommitFromSync(const CommitId& id,
                                          const std::string& storage_bytes) {
  std::unique_ptr<Commit> commit =
      CommitImpl::FromStorageBytes(id, storage_bytes);
  if (!commit) {
    return Status::FORMAT_ERROR;
  }
  return AddCommit(std::move(commit), ChangeSource::SYNC);
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
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::RemoveCommitWatcher(CommitWatcher* watcher) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetUnsyncedCommits(
    std::vector<std::unique_ptr<Commit>>* commits) {
  std::vector<CommitId> resultIds;
  Status s = db_.GetUnsyncedCommitIds(&resultIds);
  if (s != Status::OK) {
    return s;
  }

  std::vector<std::unique_ptr<Commit>> result;
  for (size_t i = 0; i < resultIds.size(); ++i) {
    std::unique_ptr<Commit> commit;
    Status s = GetCommit(resultIds[i], &commit);
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
                                        std::vector<Object>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::GetUnsyncedObjects(const CommitId& commit_id,
                                           std::vector<Object>* objects) {
  return Status::NOT_IMPLEMENTED;
}

Status PageStorageImpl::MarkObjectSynced(ObjectIdView object_id) {
  return Status::NOT_IMPLEMENTED;
}

void PageStorageImpl::AddObjectFromSync(
    ObjectIdView object_id,
    mojo::ScopedDataPipeConsumerHandle data,
    size_t size,
    const std::function<void(Status)>& callback) {
  callback(Status::NOT_IMPLEMENTED);
}

void PageStorageImpl::AddObjectFromLocal(
    mojo::ScopedDataPipeConsumerHandle data,
    size_t size,
    const std::function<void(Status, ObjectId)>& callback) {
  auto file_writer = std::make_unique<FileWriter>(staging_path_, objects_path_);
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
    cleanup = std::move(cleanup), callback = std::move(callback)
  ](Status status, ObjectId object_id) {
    callback(status, std::move(object_id));
    cleanup();
  });
}

void PageStorageImpl::GetBlob(
    ObjectIdView blob_id,
    const std::function<void(Status, std::unique_ptr<Blob>)>& callback) {
  callback(Status::NOT_IMPLEMENTED, nullptr);
}

Status PageStorageImpl::AddCommit(std::unique_ptr<Commit> commit,
                                  ChangeSource source) {
  // TODO(nellyv): Update code to use a single transaction to do all the
  // following updates in the DB.
  Status s =
      db_.AddCommitStorageBytes(commit->GetId(), commit->GetStorageBytes());
  if (s != Status::OK) {
    return s;
  }

  if (source == ChangeSource::LOCAL) {
    s = db_.MarkCommitIdUnsynced(commit->GetId());
    if (s != Status::OK) {
      return s;
    }
  }

  // Update heads.
  s = db_.AddHead(commit->GetId());
  if (s != Status::OK) {
    return s;
  }

  // TODO(nellyv): Here we assume that commits arrive in order. Change this to
  // support out of order commit arrivals.
  // Remove parents from head (if they are in heads).
  for (const CommitId& parentId : commit->GetParentIds()) {
    db_.RemoveHead(parentId);
  }
  return Status::OK;
}

}  // namespace storage
