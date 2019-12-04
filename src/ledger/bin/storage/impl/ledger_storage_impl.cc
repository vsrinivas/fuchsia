// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/ledger_storage_impl.h"

#include <dirent.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>

#include <iterator>
#include <set>
#include <string>

#include "src/ledger/bin/filesystem/directory_reader.h"
#include "src/ledger/bin/storage/impl/page_storage_impl.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/callback/trace_callback.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {

namespace {

constexpr absl::string_view kStagingDirName = "staging";

// Encodes opaque bytes in a way that is usable as a directory name.
std::string GetDirectoryName(absl::string_view bytes) { return absl::WebSafeBase64Escape(bytes); }

// Decodes opaque bytes used as a directory names into an id. This is the
// opposite transformation of GetDirectoryName.
std::string GetId(absl::string_view bytes) {
  std::string decoded;
  bool result = absl::WebSafeBase64Unescape(bytes, &decoded);
  FXL_DCHECK(result);
  return decoded;
}

}  // namespace

LedgerStorageImpl::LedgerStorageImpl(ledger::Environment* environment,
                                     encryption::EncryptionService* encryption_service,
                                     DbFactory* db_factory, ledger::DetachedPath content_dir,
                                     CommitPruningPolicy policy,
                                     clocks::DeviceIdManager* device_id_manager)
    : environment_(environment),
      encryption_service_(encryption_service),
      db_factory_(db_factory),
      storage_dir_(std::move(content_dir)),
      staging_dir_(storage_dir_.SubPath(kStagingDirName)),
      pruning_policy_(policy),
      device_id_manager_(device_id_manager),
      weak_factory_(this) {}

LedgerStorageImpl::~LedgerStorageImpl() = default;

Status LedgerStorageImpl::Init() {
  if (!environment_->file_system()->CreateDirectory(storage_dir_)) {
    FXL_LOG(ERROR) << "Failed to create the storage directory in " << storage_dir_.path();
    return Status::INTERNAL_ERROR;
  }
  return Status::OK;
}

void LedgerStorageImpl::ListPages(fit::function<void(Status, std::set<PageId>)> callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger", "ledger_storage_list_pages");
  std::set<PageId> page_ids;
  ledger::GetDirectoryEntries(storage_dir_, [&page_ids](absl::string_view encoded_page_id) {
    if (encoded_page_id != kStagingDirName) {
      page_ids.insert(GetId(encoded_page_id));
    }
    return true;
  });
  timed_callback(Status::OK, std::move(page_ids));
}

void LedgerStorageImpl::CreatePageStorage(
    PageId page_id, fit::function<void(Status, std::unique_ptr<PageStorage>)> callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "ledger_storage_create_page_storage");
  auto page_path = GetPathFor(page_id);
  GetOrCreateDb(std::move(page_path), std::move(page_id), DbFactory::OnDbNotFound::CREATE,
                std::move(timed_callback));
}

void LedgerStorageImpl::GetPageStorage(
    PageId page_id, fit::function<void(Status, std::unique_ptr<PageStorage>)> callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "ledger_storage_get_page_storage");
  auto page_path = GetPathFor(page_id);
  GetOrCreateDb(std::move(page_path), std::move(page_id), DbFactory::OnDbNotFound::RETURN,
                std::move(timed_callback));
}

void LedgerStorageImpl::DeletePageStorage(PageIdView page_id,
                                          fit::function<void(Status)> callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "ledger_storage_delete_page_storage");
  ledger::DetachedPath path = GetPathFor(page_id);
  // |final_callback| will be called from the I/O loop and call the original
  // |callback| in the main one. The main loop outlives the I/O one, so it's
  // safe to capture environment_->dispatcher() here.
  auto final_callback = [dispatcher = environment_->dispatcher(),
                         callback = std::move(timed_callback)](Status status) mutable {
    // Call the callback in the main thread.
    async::PostTask(dispatcher, [status, callback = std::move(callback)] { callback(status); });
  };

  async::PostTask(environment_->io_dispatcher(),
                  [file_system = environment_->file_system(), path = std::move(path),
                   staging_dir = staging_dir_, callback = std::move(final_callback)]() mutable {
                    if (!file_system->IsDirectory(path)) {
                      callback(Status::PAGE_NOT_FOUND);
                      return;
                    }
                    files::ScopedTempDirAt tmp_directory(staging_dir.root_fd(), staging_dir.path());
                    std::string destination = tmp_directory.path() + "/graveyard";

                    // <storage_dir_>/<base64(page)> becomes
                    // <storage_dir_>/staging/<random_temporary_name>/graveyard/<base64(page)>
                    if (renameat(path.root_fd(), path.path().c_str(), tmp_directory.root_fd(),
                                 destination.c_str()) != 0) {
                      FXL_LOG(ERROR) << "Unable to move local page storage to " << destination
                                     << ". Error: " << strerror(errno);
                      callback(Status::IO_ERROR);
                      return;
                    }

                    if (!file_system->DeletePathRecursively(
                            ledger::DetachedPath(tmp_directory.root_fd(), destination))) {
                      FXL_LOG(ERROR)
                          << "Unable to delete local staging storage at: " << destination;
                      callback(Status::IO_ERROR);
                      return;
                    }
                    callback(Status::OK);
                  });
}

void LedgerStorageImpl::InitializePageStorage(
    PageId page_id, std::unique_ptr<Db> db,
    fit::function<void(Status, std::unique_ptr<PageStorage>)> callback) {
  auto storage = std::make_unique<PageStorageImpl>(environment_, encryption_service_, std::move(db),
                                                   std::move(page_id), pruning_policy_);
  PageStorageImpl* storage_ptr = storage.get();
  storage_in_initialization_[storage_ptr] = std::move(storage);
  storage_ptr->Init(device_id_manager_, [this, callback = std::move(callback),
                                         storage_ptr](Status status) mutable {
    std::unique_ptr<PageStorage> storage = std::move(storage_in_initialization_[storage_ptr]);
    storage_in_initialization_.erase(storage_ptr);

    if (status != Status::OK) {
      FXL_LOG(ERROR) << "Failed to initialize PageStorage. Status: " << status;
      callback(status, nullptr);
      return;
    }
    callback(Status::OK, std::move(storage));
  });
}

void LedgerStorageImpl::GetOrCreateDb(
    ledger::DetachedPath path, PageId page_id, DbFactory::OnDbNotFound on_db_not_found,
    fit::function<void(Status, std::unique_ptr<PageStorage>)> callback) {
  db_factory_->GetOrCreateDb(
      std::move(path), on_db_not_found,
      callback::MakeScoped(weak_factory_.GetWeakPtr(),
                           [this, page_id = std::move(page_id), callback = std::move(callback)](
                               Status status, std::unique_ptr<Db> db) mutable {
                             if (status != Status::OK) {
                               callback(status, nullptr);
                               return;
                             }
                             InitializePageStorage(std::move(page_id), std::move(db),
                                                   std::move(callback));
                           }));
}

ledger::DetachedPath LedgerStorageImpl::GetPathFor(PageIdView page_id) {
  FXL_DCHECK(!page_id.empty());
  return storage_dir_.SubPath(GetDirectoryName(page_id));
}

}  // namespace storage
