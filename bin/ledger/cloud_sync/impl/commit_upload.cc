// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/impl/commit_upload.h"

#include "apps/ledger/src/cloud_provider/public/commit.h"
#include "apps/ledger/src/cloud_provider/public/types.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/vmo/strings.h"

namespace cloud_sync {

CommitUpload::CommitUpload(storage::PageStorage* storage,
                           cloud_provider::CloudProvider* cloud_provider,
                           std::unique_ptr<const storage::Commit> commit,
                           ftl::Closure on_done,
                           ftl::Closure on_error)
    : storage_(storage),
      cloud_provider_(cloud_provider),
      commit_(std::move(commit)),
      on_done_(on_done),
      on_error_(on_error) {
  FTL_DCHECK(storage);
  FTL_DCHECK(cloud_provider);
}

CommitUpload::~CommitUpload() {}

void CommitUpload::Start() {
  FTL_DCHECK(!active_or_finished_);
  current_attempt_++;
  active_or_finished_ = true;

  std::vector<storage::ObjectId> object_ids;
  auto storage_status =
      storage_->GetUnsyncedObjects(commit_->GetId(), &object_ids);
  FTL_DCHECK(storage_status == storage::Status::OK);

  // If there are no unsynced objects referenced by the commit, upload the
  // commit directly.
  if (object_ids.empty()) {
    UploadCommit();
    return;
  }

  // Upload all unsynced objects referenced by the commit. The last upload that
  // succeeds triggers uploading the commit.
  objects_to_upload_ = object_ids.size();
  for (const auto& id : object_ids) {
    storage_->GetObject(id,
                        [this](storage::Status storage_status,
                               std::unique_ptr<const storage::Object> object) {
                          FTL_DCHECK(storage_status == storage::Status::OK);
                          UploadObject(std::move(object));
                        });
  }
}

void CommitUpload::UploadObject(std::unique_ptr<const storage::Object> object) {
  ftl::StringView data_view;
  auto status = object->GetData(&data_view);
  FTL_DCHECK(status == storage::Status::OK);

  // TODO(ppi): get the virtual memory object directly from storage::Object,
  // once it can give us one.
  mx::vmo data;
  auto result = mtl::VmoFromString(data_view, &data);
  FTL_DCHECK(result);

  storage::ObjectId id = object->GetId();
  cloud_provider_->AddObject(object->GetId(), std::move(data), [
    this, id = std::move(id), upload_attempt = current_attempt_
  ](cloud_provider::Status status) {
    if (upload_attempt != current_attempt_) {
      // Object upload was completed for a previous .Start() call. If it
      // succeeded, we still mark it as synced, as this allows to avoid
      // re-uploading this object upon the next upload attempt.
      if (status == cloud_provider::Status::OK) {
        storage_->MarkObjectSynced(id);
      }
      return;
    }

    if (status != cloud_provider::Status::OK) {
      if (active_or_finished_) {
        active_or_finished_ = false;
        on_error_();
      }
      return;
    }
    storage_->MarkObjectSynced(id);
    objects_to_upload_--;
    if (objects_to_upload_ == 0) {
      // All the referenced objects are uploaded, upload the commit.
      UploadCommit();
    }
  });
}

void CommitUpload::UploadCommit() {
  cloud_provider::Commit commit(
      commit_->GetId(), commit_->GetStorageBytes(),
      std::map<cloud_provider::ObjectId, cloud_provider::Data>{});
  storage::CommitId commit_id = commit_->GetId();
  cloud_provider_->AddCommit(commit, [ this, commit_id = std::move(commit_id) ](
                                         cloud_provider::Status status) {
    // UploadCommit() is called as a last step of a so-far-successful upload
    // attempt, so we couldn't have failed before.
    FTL_DCHECK(active_or_finished_);
    if (status != cloud_provider::Status::OK) {
      active_or_finished_ = false;
      on_error_();
      return;
    }
    storage_->MarkCommitSynced(commit_id);
    on_done_();
  });
}

}  // namespace cloud_sync
