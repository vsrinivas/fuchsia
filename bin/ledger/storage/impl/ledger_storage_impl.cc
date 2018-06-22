// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/ledger_storage_impl.h"

#include <dirent.h>
#include <algorithm>
#include <iterator>

#include "lib/callback/trace_callback.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "peridot/bin/ledger/filesystem/directory_reader.h"
#include "peridot/bin/ledger/storage/impl/page_storage_impl.h"
#include "peridot/bin/ledger/storage/public/constants.h"
#include "peridot/lib/base64url/base64url.h"

namespace storage {

namespace {

// Encodes opaque bytes in a way that is usable as a directory name.
std::string GetDirectoryName(fxl::StringView bytes) {
  return base64url::Base64UrlEncode(bytes);
}

// Decodes opaque bytes used as a directory names into an id. This is the
// opposite transformation of GetDirectoryName.
std::string GetId(fxl::StringView bytes) {
  std::string decoded;
  bool result = base64url::Base64UrlDecode(bytes, &decoded);
  FXL_DCHECK(result);
  return decoded;
}

}  // namespace

LedgerStorageImpl::LedgerStorageImpl(
    async_t* async, coroutine::CoroutineService* coroutine_service,
    encryption::EncryptionService* encryption_service,
    ledger::DetachedPath content_dir, const std::string& ledger_name)
    : async_(async),
      coroutine_service_(coroutine_service),
      encryption_service_(encryption_service),
      storage_dir_(content_dir.SubPath(
          {kSerializationVersion, GetDirectoryName(ledger_name)})) {}

LedgerStorageImpl::~LedgerStorageImpl() {}

void LedgerStorageImpl::CreatePageStorage(
    PageId page_id,
    std::function<void(Status, std::unique_ptr<PageStorage>)> callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger",
                                       "ledger_storage_create_page_storage");
  ledger::DetachedPath path = GetPathFor(page_id);
  if (!files::CreateDirectoryAt(path.root_fd(), path.path())) {
    FXL_LOG(ERROR) << "Failed to create the storage directory in "
                   << path.path();
    timed_callback(Status::INTERNAL_IO_ERROR, nullptr);
    return;
  }
  auto result = std::make_unique<PageStorageImpl>(
      async_, coroutine_service_, encryption_service_, std::move(path),
      std::move(page_id));
  result->Init(
      fxl::MakeCopyable([callback = std::move(timed_callback),
                         result = std::move(result)](Status status) mutable {
        if (status != Status::OK) {
          FXL_LOG(ERROR) << "Failed to initialize PageStorage. Status: "
                         << status;
          callback(status, nullptr);
          return;
        }
        callback(Status::OK, std::move(result));
      }));
}

void LedgerStorageImpl::GetPageStorage(
    PageId page_id,
    std::function<void(Status, std::unique_ptr<PageStorage>)> callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger",
                                       "ledger_storage_get_page_storage");
  ledger::DetachedPath path = GetPathFor(page_id);
  if (!files::IsDirectoryAt(path.root_fd(), path.path())) {
    timed_callback(Status::NOT_FOUND, nullptr);
    return;
  }

  auto result = std::make_unique<PageStorageImpl>(
      async_, coroutine_service_, encryption_service_, std::move(path),
      std::move(page_id));
  result->Init(
      fxl::MakeCopyable([callback = std::move(timed_callback),
                         result = std::move(result)](Status status) mutable {
        if (status != Status::OK) {
          callback(status, nullptr);
          return;
        }
        callback(status, std::move(result));
      }));
}

bool LedgerStorageImpl::DeletePageStorage(PageIdView page_id) {
  ledger::DetachedPath path = GetPathFor(page_id);
  if (!files::IsDirectoryAt(path.root_fd(), path.path())) {
    return false;
  }
  if (!files::DeletePathAt(path.root_fd(), path.path(), true)) {
    FXL_LOG(ERROR) << "Unable to delete: " << path.path();
    return false;
  }
  return true;
}

std::vector<PageId> LedgerStorageImpl::ListLocalPages() {
  std::vector<PageId> local_pages;
  ledger::DirectoryReader::GetDirectoryEntriesAt(
      storage_dir_, [&local_pages](fxl::StringView encoded_page_id) {
        local_pages.emplace_back(GetId(encoded_page_id));
        return true;
      });
  return local_pages;
}

ledger::DetachedPath LedgerStorageImpl::GetPathFor(PageIdView page_id) {
  FXL_DCHECK(!page_id.empty());
  return storage_dir_.SubPath(GetDirectoryName(page_id));
}

}  // namespace storage
