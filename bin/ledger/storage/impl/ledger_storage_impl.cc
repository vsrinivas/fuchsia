// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/ledger_storage_impl.h"

#include <algorithm>
#include <iterator>

#include "apps/ledger/src/glue/crypto/base64.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"

namespace storage {

namespace {
const char kVersion[] = "0.0.1";

// Encodes opaque bytes in a way that is usable as a directory name.
std::string GetDirectoryName(ftl::StringView bytes) {
  // TODO(ppi): switch to a method that needs only one pass.
  std::string encoded;
  glue::Base64Encode(bytes, &encoded);
  std::replace(std::begin(encoded), std::end(encoded), '/', '_');
  std::replace(std::begin(encoded), std::end(encoded), '+', '.');
  return encoded;
}
}

LedgerStorageImpl::LedgerStorageImpl(ftl::RefPtr<ftl::TaskRunner> task_runner,
                                     const std::string& base_storage_dir,
                                     const Identity& identity)
    : task_runner_(std::move(task_runner)) {
  storage_dir_ = base_storage_dir + "/" + kVersion + "/" +
                 GetDirectoryName(identity.user_id) + "/" +
                 GetDirectoryName(identity.app_id);
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
      new PageStorageImpl(task_runner_, GetPathFor(page_id), page_id));
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
    const std::function<void(Status, std::unique_ptr<PageStorage>)>& callback) {
  std::string path = GetPathFor(page_id);
  if (files::IsDirectory(path)) {
    task_runner_->PostTask([ this, callback, page_id = page_id.ToString() ]() {
      std::unique_ptr<PageStorageImpl> result(
          new PageStorageImpl(task_runner_, GetPathFor(page_id), page_id));
      Status status = result->Init();
      if (status != Status::OK) {
        callback(status, nullptr);
        return;
      }
      callback(status, std::move(result));
    });
    return;
  }
  // TODO(nellyv): Maybe the page exists but is not synchronized, yet. We need
  // to check in the cloud.
  task_runner_->PostTask(
      [callback]() { callback(Status::NOT_FOUND, nullptr); });
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
  return storage_dir_ + "/" + GetDirectoryName(page_id);
}

}  // namespace storage
