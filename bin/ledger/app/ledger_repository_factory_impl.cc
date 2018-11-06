// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/ledger_repository_factory_impl.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <lib/backoff/exponential_backoff.h>
#include <lib/component/cpp/object_dir.h>
#include <lib/fdio/util.h>
#include <lib/fit/function.h>
#include <lib/fsl/io/fd.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/eintr_wrapper.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/files/scoped_temp_dir.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/string_view.h>
#include <trace/event.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "peridot/bin/ledger/app/constants.h"
#include "peridot/bin/ledger/app/disk_cleanup_manager_impl.h"
#include "peridot/bin/ledger/cloud_sync/impl/user_sync_impl.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/p2p_provider/impl/p2p_provider_impl.h"
#include "peridot/bin/ledger/p2p_provider/impl/user_id_provider_impl.h"
#include "peridot/bin/ledger/p2p_sync/impl/user_communicator_impl.h"
#include "peridot/bin/ledger/storage/impl/leveldb_factory.h"
#include "peridot/bin/ledger/sync_coordinator/impl/user_sync_impl.h"

namespace ledger {

namespace {

// The contents of each repository are organized in the following way:
//   <base_path>
//   ├── content/
//   │   ├── name
//   │   ├── cache/
//   │   ├── page_usage_db/
//   │   └── ...
//   └── staging/
//
// - <base_path>/
//   The base path of this repository. It is defined by the channel given in
//   |LedgerRepositoryFactory::GetRepository| (see the internal.fidl API).
// - <base_path>/content/
//   Contains all the contents of this repository. It is used to store the
//   `name` file, `page_usage_db/` (see below), and is also used by
//   |LedgerRepositoryImpl| to store this repository's Ledger instances.
// - <base_path>/content/name
//   Stores the name of the repository, which is randomly chosen on creation.
// - <base_path>/content/cache/
//   The path used by |LevelDbFactory| as the cache directory.
// - <base_path>/content/page_usage_db/
//   The path used by |DiskCleanupManagerImpl| to store statistics on pages.
// - <base_path>/staging/
//   The staging path. Used for removing all contents of this repository.
//
// Note that content/ should be the only directory storing information on the
// repository: When deleting a repository, the content/ directory is moved
// atomically to the staging path and then contents are recursively deleted.
// This two-phase deletion guarantees that the repository will be in a correct
// state even if the deletion execution is unexpectedly terminated.

constexpr fxl::StringView kContentPath = "content";
constexpr fxl::StringView kCachePath = "cache";
constexpr fxl::StringView kPageUsageDbPath = "page_usage_db";
constexpr fxl::StringView kStagingPath = "staging";
constexpr fxl::StringView kNamePath = "name";

constexpr char kRepositoriesPath[] = "repositories";

bool GetRepositoryName(rng::Random* random, const DetachedPath& content_path,
                       std::string* name) {
  DetachedPath name_path = content_path.SubPath(kNamePath);

  if (files::ReadFileToStringAt(name_path.root_fd(), name_path.path(), name)) {
    return true;
  }

  if (!files::CreateDirectoryAt(content_path.root_fd(), content_path.path())) {
    return false;
  }

  std::string new_name;
  new_name.resize(16);
  random->Draw(&new_name);
  if (!files::WriteFileAt(name_path.root_fd(), name_path.path(),
                          new_name.c_str(), new_name.size())) {
    FXL_LOG(ERROR) << "Unable to write file at: " << name_path.path();
    return false;
  }

  name->swap(new_name);
  return true;
}

}  // namespace

// Container for a LedgerRepositoryImpl that keeps track of the in-flight FIDL
// requests and callbacks and fires them when the repository is available.
class LedgerRepositoryFactoryImpl::LedgerRepositoryContainer {
 public:
  explicit LedgerRepositoryContainer(fxl::UniqueFD root_fd)
      : root_fd_(std::move(root_fd)) {}
  ~LedgerRepositoryContainer() {
    for (const auto& request : requests_) {
      request.second(Status::INTERNAL_ERROR);
    }
  }

  void set_on_empty(fit::closure on_empty_callback) {
    if (ledger_repository_) {
      ledger_repository_->set_on_empty(std::move(on_empty_callback));
    } else {
      on_empty_callback_ = std::move(on_empty_callback);
    }
  };

  // Keeps track of |request| and |callback|. Binds |request| and fires
  // |callback| when the repository is available or an error occurs.
  void BindRepository(
      fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
      fit::function<void(Status)> callback) {
    if (status_ != Status::OK) {
      callback(status_);
      return;
    }
    if (ledger_repository_) {
      ledger_repository_->BindRepository(std::move(request));
      callback(status_);
      return;
    }
    requests_.emplace_back(std::move(request), std::move(callback));
  }

  // Sets the implementation or the error status for the container. This
  // notifies all awaiting callbacks and binds all pages in case of success.
  void SetRepository(Status status,
                     std::unique_ptr<LedgerRepositoryImpl> ledger_repository) {
    FXL_DCHECK(!ledger_repository_);
    FXL_DCHECK(status != Status::OK || ledger_repository);
    status_ = status;
    ledger_repository_ = std::move(ledger_repository);
    for (auto& request : requests_) {
      if (ledger_repository_) {
        ledger_repository_->BindRepository(std::move(request.first));
      }
      request.second(status_);
    }
    requests_.clear();
    if (on_empty_callback_) {
      if (ledger_repository_) {
        ledger_repository_->set_on_empty(std::move(on_empty_callback_));
      } else {
        on_empty_callback_();
      }
    }
  }

  // Shuts down the repository impl (if already initialized) and detaches all
  // handles bound to it, moving their owneship to the container.
  void Detach() {
    if (ledger_repository_) {
      detached_handles_ = ledger_repository_->Unbind();
      ledger_repository_.reset();
    }
    for (auto& request : requests_) {
      detached_handles_.push_back(std::move(request.first));
    }

    // TODO(ppi): rather than failing all already pending and future requests,
    // we should stash them and fulfill them once the deletion is finished.
    status_ = Status::INTERNAL_ERROR;
  }

 private:
  fxl::UniqueFD root_fd_;
  std::unique_ptr<LedgerRepositoryImpl> ledger_repository_;
  Status status_ = Status::OK;
  std::vector<
      std::pair<fidl::InterfaceRequest<ledger_internal::LedgerRepository>,
                fit::function<void(Status)>>>
      requests_;
  fit::closure on_empty_callback_;
  std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>>
      detached_handles_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryContainer);
};

struct LedgerRepositoryFactoryImpl::RepositoryInformation {
 public:
  explicit RepositoryInformation(int root_fd)
      : base_path(root_fd),
        content_path(base_path.SubPath(kContentPath)),
        cache_path(content_path.SubPath(kCachePath)),
        page_usage_db_path(content_path.SubPath(kPageUsageDbPath)),
        staging_path(base_path.SubPath(kStagingPath)) {}

  RepositoryInformation(const RepositoryInformation& other) = default;
  RepositoryInformation(RepositoryInformation&& other) = default;

  bool Init(rng::Random* random) {
    return GetRepositoryName(random, content_path, &name);
  }

  DetachedPath base_path;
  DetachedPath content_path;
  DetachedPath cache_path;
  DetachedPath page_usage_db_path;
  DetachedPath staging_path;
  std::string name;
};

LedgerRepositoryFactoryImpl::LedgerRepositoryFactoryImpl(
    Environment* environment,
    std::unique_ptr<p2p_sync::UserCommunicatorFactory>
        user_communicator_factory,
    component::ObjectDir inspect_object_dir)
    : environment_(environment),
      user_communicator_factory_(std::move(user_communicator_factory)),
      inspect_object_dir_(std::move(inspect_object_dir)) {}

LedgerRepositoryFactoryImpl::~LedgerRepositoryFactoryImpl() {}

void LedgerRepositoryFactoryImpl::GetRepository(
    zx::channel repository_handle,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
    fidl::InterfaceRequest<ledger_internal::LedgerRepository>
        repository_request,
    fit::function<void(Status)> callback) {
  fxl::UniqueFD root_fd =
      fsl::OpenChannelAsFileDescriptor(std::move(repository_handle));
  if (!root_fd.is_valid()) {
    callback(Status::IO_ERROR);
    return;
  }
  GetRepositoryByFD(std::move(root_fd), std::move(cloud_provider),
                    std::move(repository_request), std::move(callback));
}

void LedgerRepositoryFactoryImpl::GetRepositoryByFD(
    fxl::UniqueFD root_fd,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
    fidl::InterfaceRequest<ledger_internal::LedgerRepository>
        repository_request,
    fit::function<void(Status)> callback) {
  TRACE_DURATION("ledger", "repository_factory_get_repository");

  RepositoryInformation repository_information(root_fd.get());
  if (!repository_information.Init(environment_->random())) {
    callback(Status::IO_ERROR);
    return;
  }

  auto it = repositories_.find(repository_information.name);
  if (it != repositories_.end()) {
    it->second.BindRepository(std::move(repository_request),
                              std::move(callback));
    return;
  }

  auto ret =
      repositories_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(repository_information.name),
                            std::forward_as_tuple(std::move(root_fd)));
  LedgerRepositoryContainer* container = &ret.first->second;
  container->BindRepository(std::move(repository_request), std::move(callback));

  auto disk_cleanup_manager = std::make_unique<DiskCleanupManagerImpl>(
      environment_, repository_information.page_usage_db_path);
  Status status = disk_cleanup_manager->Init();
  if (status != Status::OK) {
    container->SetRepository(status, nullptr);
    return;
  }

  std::unique_ptr<SyncWatcherSet> watchers = std::make_unique<SyncWatcherSet>();
  std::unique_ptr<sync_coordinator::UserSyncImpl> user_sync;
  if (cloud_provider) {
    user_sync = CreateUserSync(repository_information,
                               std::move(cloud_provider), watchers.get());
  } else {
    FXL_LOG(WARNING) << "No cloud provider - Ledger will work locally but "
                     << "not sync. (running in Guest mode?)";
  }

  DiskCleanupManagerImpl* disk_cleanup_manager_ptr = disk_cleanup_manager.get();
  auto db_factory = std::make_unique<storage::LevelDbFactory>(
      environment_, repository_information.cache_path);
  db_factory->Init();
  component::ExposedObject repository_exposed_object(
      convert::ToHex(repository_information.name));
  repository_exposed_object.set_parent(inspect_object_dir_.find({kRepositoriesPath}));
  auto repository = std::make_unique<LedgerRepositoryImpl>(
      std::move(repository_exposed_object), repository_information.content_path,
      environment_, std::move(db_factory), std::move(watchers),
      std::move(user_sync), std::move(disk_cleanup_manager),
      disk_cleanup_manager_ptr);
  disk_cleanup_manager_ptr->SetPageEvictionDelegate(repository.get());
  container->SetRepository(Status::OK, std::move(repository));
}

std::unique_ptr<sync_coordinator::UserSyncImpl>
LedgerRepositoryFactoryImpl::CreateUserSync(
    const RepositoryInformation& repository_information,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
    SyncWatcherSet* watchers) {
  auto cloud_provider_ptr = cloud_provider.Bind();
  cloud_provider_ptr.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR)
        << "Lost connection to cloud provider; cloud sync will no longer work.";
  });

  cloud_sync::UserConfig user_config;
  user_config.user_directory = repository_information.content_path;
  user_config.cloud_provider = std::move(cloud_provider_ptr);
  fit::closure on_version_mismatch = [this, repository_information]() mutable {
    OnVersionMismatch(std::move(repository_information));
  };
  auto cloud_sync = std::make_unique<cloud_sync::UserSyncImpl>(
      environment_, std::move(user_config), environment_->MakeBackoff(),
      std::move(on_version_mismatch));
  std::unique_ptr<p2p_sync::UserCommunicator> p2p_sync =
      CreateP2PSync(repository_information);

  auto user_sync = std::make_unique<sync_coordinator::UserSyncImpl>(
      std::move(cloud_sync), std::move(p2p_sync));
  user_sync->SetWatcher(watchers);
  user_sync->Start();
  return user_sync;
}

std::unique_ptr<p2p_sync::UserCommunicator>
LedgerRepositoryFactoryImpl::CreateP2PSync(
    const RepositoryInformation& repository_information) {
  if (!user_communicator_factory_) {
    return nullptr;
  }

  return user_communicator_factory_->GetUserCommunicator(
      repository_information.content_path);
}

void LedgerRepositoryFactoryImpl::OnVersionMismatch(
    RepositoryInformation repository_information) {
  FXL_LOG(WARNING)
      << "Data in the cloud was wiped out, erasing local state. "
      << "This should log you out, log back in to start syncing again.";

  // First, shut down the repository so that we can delete the files while it's
  // not running.
  auto find_repository = repositories_.find(repository_information.name);
  FXL_DCHECK(find_repository != repositories_.end());
  find_repository->second.Detach();
  DeleteRepositoryDirectory(repository_information);
  repositories_.erase(find_repository);
}

Status LedgerRepositoryFactoryImpl::DeleteRepositoryDirectory(
    const RepositoryInformation& repository_information) {
  files::ScopedTempDirAt tmp_directory(
      repository_information.staging_path.root_fd(),
      repository_information.staging_path.path());
  std::string destination = tmp_directory.path() + "/content";

  if (renameat(repository_information.content_path.root_fd(),
               repository_information.content_path.path().c_str(),
               tmp_directory.root_fd(), destination.c_str()) != 0) {
    FXL_LOG(ERROR) << "Unable to move repository local storage to "
                   << destination << ". Error: " << strerror(errno);
    return Status::IO_ERROR;
  }
  if (!files::DeletePathAt(tmp_directory.root_fd(), destination, true)) {
    FXL_LOG(ERROR) << "Unable to delete repository staging storage at "
                   << destination;
    return Status::IO_ERROR;
  }
  return Status::OK;
}

}  // namespace ledger
