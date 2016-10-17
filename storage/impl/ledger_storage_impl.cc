// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/ledger_storage_impl.h"

#include "apps/ledger/glue/crypto/base64.h"
#include "apps/ledger/storage/impl/page_storage_impl.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"

namespace storage {

LedgerStorageImpl::LedgerStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                     const std::string& base_storage_dir,
                                     const std::string& identity)
    : task_runner_(std::move(task_runner)) {
  storage_dir_ = base_storage_dir + "/" + identity;
}

LedgerStorageImpl::~LedgerStorageImpl() {}

Status LedgerStorageImpl::CreatePageStorage(
    PageIdView page_id,
    std::unique_ptr<PageStorage>* page_storage) {
  std::string path = GetPathFor(page_id);
  if (!files::CreateDirectory(path)) {
    FTL_LOG(ERROR) << "Failed to create the storage directory in " << path;
    return Status::INTERNAL_IO_ERROR;
  }
  std::unique_ptr<PageStorageImpl> result(
      new PageStorageImpl(GetPathFor(page_id), page_id));
  Status s = result->Init();
  if (s != Status::OK) {
    FTL_LOG(ERROR) << "Failed to initialize PageStorage.";
    return s;
  }
  *page_storage = std::move(result);
  return Status::OK;
}

void LedgerStorageImpl::GetPageStorage(
    PageIdView page_id,
    const std::function<void(std::unique_ptr<PageStorage>)>& callback) {
  std::string path = GetPathFor(page_id);
  if (files::IsDirectory(path)) {
    task_runner_->PostTask([this, callback, page_id]() {
      callback(std::unique_ptr<PageStorage>(
          new PageStorageImpl(GetPathFor(page_id), page_id)));
    });
    return;
  }
  // TODO(nellyv): Maybe the page exists but is not synchronized, yet. We need
  // to check in the cloud.
  task_runner_->PostTask([callback]() { callback(nullptr); });
}

bool LedgerStorageImpl::DeletePageStorage(PageIdView page_id) {
  // TODO(nellyv): We need to synchronize the page deletion with the cloud.
  std::string path = GetPathFor(page_id);
  if (!files::IsDirectory(path)) {
    return false;
  }
  if (!files::DeletePath(path, true)) {
    FTL_LOG(ERROR) << "Unable to delete: " << path;
    return false;
  }
  return true;
}

std::string LedgerStorageImpl::GetPathFor(PageIdView page_id) {
  FTL_DCHECK(!page_id.empty());
  std::string encoded;
  glue::Base64Encode(page_id, &encoded);
  return storage_dir_ + "/" + encoded;
}

}  // namespace storage
