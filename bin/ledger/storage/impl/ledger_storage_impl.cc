// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/ledger_storage_impl.h"

#include <algorithm>
#include <iterator>

#include "apps/ledger/src/glue/crypto/base64.h"
#include "apps/ledger/src/storage/impl/page_storage_impl.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/concatenate.h"

namespace storage {

namespace {

// Encodes opaque bytes in a way that is usable as a directory name.
std::string GetDirectoryName(ftl::StringView bytes) {
  // TODO(ppi): switch to a method that needs only one pass.
  std::string encoded;
  glue::Base64Encode(bytes, &encoded);
  for (auto it = encoded.begin(); it != encoded.end(); ++it) {
    switch (*it) {
      case '/':
        *it = '_';
        break;
      case '+':
        *it = '.';
        break;
    }
  }
  return encoded;
}
}

LedgerStorageImpl::LedgerStorageImpl(ftl::RefPtr<ftl::TaskRunner> main_runner,
                                     ftl::RefPtr<ftl::TaskRunner> io_runner,
                                     const std::string& base_storage_dir,
                                     const std::string& ledger_name)
    : main_runner_(std::move(main_runner)), io_runner_(std::move(io_runner)) {
  storage_dir_ = ftl::Concatenate({base_storage_dir, "/", kSerializationVersion,
                                   "/", GetDirectoryName(ledger_name)});
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
  auto result = std::make_unique<PageStorageImpl>(main_runner_, io_runner_,
                                                  GetPathFor(page_id), page_id);
  Status s = result->Init();
  if (s != Status::OK) {
    FTL_LOG(ERROR) << "Failed to initialize PageStorage. Status: " << s;
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
    auto result = std::make_unique<PageStorageImpl>(
        main_runner_, io_runner_, GetPathFor(page_id), page_id);
    Status status = result->Init();
    if (status != Status::OK) {
      callback(status, nullptr);
      return;
    }
    callback(status, std::move(result));
    return;
  }
  // TODO(nellyv): Maybe the page exists but is not synchronized, yet. We need
  // to check in the cloud.
  callback(Status::NOT_FOUND, nullptr);
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
  return ftl::Concatenate({storage_dir_, "/", GetDirectoryName(page_id)});
}

}  // namespace storage
