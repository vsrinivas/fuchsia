// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_factory_impl.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <lib/async/wait.h>
#include <lib/backoff/exponential_backoff.h>
#include <lib/component/cpp/expose.h>
#include <lib/component/cpp/object_dir.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/function.h>
#include <lib/fsl/io/fd.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/fxl/strings/string_view.h>
#include <lib/inspect/inspect.h>
#include <trace/event.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/app/serialization_version.h"
#include "src/ledger/bin/cloud_sync/impl/user_sync_impl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"
#include "src/ledger/bin/p2p_provider/impl/static_user_id_provider.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_impl.h"
#include "src/ledger/bin/storage/impl/leveldb_factory.h"
#include "src/ledger/bin/sync_coordinator/impl/user_sync_impl.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/eintr_wrapper.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace ledger {

namespace {

// The contents of each repository are organized in the following way:
//   <base_path>
//   ├── <serialization_version>
//   │   ├── name
//   │   ├── cache/
//   │   ├── page_usage_db/
//   │   └── ledgers
//   │       └── ...
//   └── staging/
//
// - <base_path>/
//   The base path of this repository. It is defined by the channel given in
//   |LedgerRepositoryFactory::GetRepository| (see the internal.fidl API).
// - <base_path>/<serialization_version>/
//   Stores all the contents of this repository for that serialization
//   version. It is used to store the `name` file, and subdirectories `cache/`,
//   `page_usage_db/` and `ledgers/` (see below).
// - <base_path>/<serialization_version>/name
//   Stores the name of the repository, which is randomly chosen on creation.
// - <base_path>/<serialization_version>/cache/
//   The path used by |LevelDbFactory| as the cache directory.
// - <base_path>/<serialization_version>/page_usage_db/
//   The path used by |DiskCleanupManagerImpl| to store statistics on pages.
// - <base_path>/<serialization_version>/ledgers/
//   The path used by |LedgerRepositoryImpl| to store all Ledger instances for
//   this repository.
// - <base_path>/staging/
//   The staging path. Used for removing all contents of this repository.
//
// Note that <serialization_version>/ should be the only directory storing
// information on the repository; when deleting a repository, the
// <serialization_version>/ directory is moved atomically to the staging path
// and then contents are recursively deleted. This two-phase deletion guarantees
// that the repository will be in a correct state even if the deletion execution
// is unexpectedly terminated.

constexpr fxl::StringView kCachePath = "cache";
constexpr fxl::StringView kPageUsageDbPath = "page_usage_db";
constexpr fxl::StringView kLedgersPath = "ledgers";
constexpr fxl::StringView kStagingPath = "staging";
constexpr fxl::StringView kNamePath = "name";

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
      : root_fd_(std::move(root_fd)) {
    // Ensure that we close the repository if the underlying filesystem closes
    // too. This prevents us from trying to write on disk when there's no disk
    // anymore. This situation can happen when the Ledger is shut down, if the
    // storage is shut down at the same time.
    fd_chan_ = fsl::CloneChannelFromFileDescriptor(root_fd_.get());
    fd_wait_ = std::make_unique<async::Wait>(
        fd_chan_.get(), ZX_CHANNEL_PEER_CLOSED,
        [this](async_dispatcher_t* dispatcher, async::WaitBase* wait,
               zx_status_t status,
               const zx_packet_signal* signal) { on_empty(); });
    zx_status_t status = fd_wait_->Begin(async_get_default_dispatcher());
    FXL_DCHECK(status == ZX_OK);
  }

  ~LedgerRepositoryContainer() {
    for (const auto& request : requests_) {
      request.second(storage::Status::INTERNAL_ERROR);
    }
  }

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  };

  // Keeps track of |request| and |callback|. Binds |request| and fires
  // |callback| when the repository is available or an error occurs.
  void BindRepository(
      fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
      fit::function<void(storage::Status)> callback) {
    if (status_ != storage::Status::OK) {
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
  void SetRepository(storage::Status status,
                     std::unique_ptr<LedgerRepositoryImpl> ledger_repository) {
    FXL_DCHECK(!ledger_repository_);
    FXL_DCHECK(status != storage::Status::OK || ledger_repository);
    status_ = status;
    ledger_repository_ = std::move(ledger_repository);
    for (auto& request : requests_) {
      if (ledger_repository_) {
        ledger_repository_->BindRepository(std::move(request.first));
      }
      request.second(status_);
    }
    requests_.clear();
    if (ledger_repository_) {
      ledger_repository_->set_on_empty([this] { on_empty(); });
    } else {
      on_empty();
    }
  }

  // Shuts down the repository impl (if already initialized) and detaches all
  // handles bound to it, moving their ownership to the container.
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
    status_ = storage::Status::INTERNAL_ERROR;
  }

 private:
  void on_empty() {
    if (on_empty_callback_) {
      on_empty_callback_();
    }
  }

  fxl::UniqueFD root_fd_;
  zx::channel fd_chan_;
  std::unique_ptr<async::Wait> fd_wait_;
  std::unique_ptr<LedgerRepositoryImpl> ledger_repository_;
  storage::Status status_ = storage::Status::OK;
  std::vector<
      std::pair<fidl::InterfaceRequest<ledger_internal::LedgerRepository>,
                fit::function<void(storage::Status)>>>
      requests_;
  fit::closure on_empty_callback_;
  std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>>
      detached_handles_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerRepositoryContainer);
};

struct LedgerRepositoryFactoryImpl::RepositoryInformation {
 public:
  explicit RepositoryInformation(int root_fd, std::string user_id)
      : base_path(root_fd),
        content_path(base_path.SubPath(kSerializationVersion)),
        cache_path(content_path.SubPath(kCachePath)),
        page_usage_db_path(content_path.SubPath(kPageUsageDbPath)),
        ledgers_path(content_path.SubPath(kLedgersPath)),
        staging_path(base_path.SubPath(kStagingPath)),
        user_id(std::move(user_id)) {}

  RepositoryInformation(const RepositoryInformation& other) = default;
  RepositoryInformation(RepositoryInformation&& other) = default;

  bool Init(rng::Random* random) {
    return GetRepositoryName(random, content_path, &name);
  }

  DetachedPath base_path;
  DetachedPath content_path;
  DetachedPath cache_path;
  DetachedPath page_usage_db_path;
  DetachedPath ledgers_path;
  DetachedPath staging_path;
  std::string user_id;
  std::string name;
};

LedgerRepositoryFactoryImpl::LedgerRepositoryFactoryImpl(
    Environment* environment,
    std::unique_ptr<p2p_sync::UserCommunicatorFactory>
        user_communicator_factory,
    inspect::Object inspect_object)
    : environment_(environment),
      user_communicator_factory_(std::move(user_communicator_factory)),
      inspect_object_(std::move(inspect_object)) {}

LedgerRepositoryFactoryImpl::~LedgerRepositoryFactoryImpl() {}

void LedgerRepositoryFactoryImpl::GetRepository(
    zx::channel repository_handle,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
    std::string user_id,
    fidl::InterfaceRequest<ledger_internal::LedgerRepository>
        repository_request,
    fit::function<void(Status)> callback) {
  fxl::UniqueFD root_fd =
      fsl::OpenChannelAsFileDescriptor(std::move(repository_handle));
  if (!root_fd.is_valid()) {
    callback(Status::IO_ERROR);
    return;
  }
  GetRepositoryByFD(std::move(root_fd), std::move(cloud_provider), user_id,
                    std::move(repository_request),
                    PageUtils::AdaptStatusCallback(std::move(callback)));
}

void LedgerRepositoryFactoryImpl::GetRepositoryByFD(
    fxl::UniqueFD root_fd,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
    std::string user_id,
    fidl::InterfaceRequest<ledger_internal::LedgerRepository>
        repository_request,
    fit::function<void(storage::Status)> callback) {
  TRACE_DURATION("ledger", "repository_factory_get_repository");

  RepositoryInformation repository_information(root_fd.get(),
                                               std::move(user_id));
  if (!repository_information.Init(environment_->random())) {
    callback(storage::Status::IO_ERROR);
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

  auto db_factory = std::make_unique<storage::LevelDbFactory>(
      environment_, repository_information.cache_path);
  db_factory->Init();
  auto disk_cleanup_manager = std::make_unique<DiskCleanupManagerImpl>(
      environment_, db_factory.get(),
      repository_information.page_usage_db_path);
  storage::Status status = disk_cleanup_manager->Init();
  if (status != storage::Status::OK) {
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
  auto repository = std::make_unique<LedgerRepositoryImpl>(
      repository_information.ledgers_path, environment_, std::move(db_factory),
      std::move(watchers), std::move(user_sync),
      std::move(disk_cleanup_manager), disk_cleanup_manager_ptr,
      inspect_object_.CreateChild(convert::ToHex(repository_information.name)));
  disk_cleanup_manager_ptr->SetPageEvictionDelegate(repository.get());
  container->SetRepository(storage::Status::OK, std::move(repository));
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
    OnVersionMismatch(repository_information);
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

  if (repository_information.user_id.empty()) {
    return nullptr;
  }

  auto user_id_provider = std::make_unique<p2p_provider::StaticUserIdProvider>(
      repository_information.user_id);

  return user_communicator_factory_->GetUserCommunicator(
      std::move(user_id_provider));
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

storage::Status LedgerRepositoryFactoryImpl::DeleteRepositoryDirectory(
    const RepositoryInformation& repository_information) {
  files::ScopedTempDirAt tmp_directory(
      repository_information.staging_path.root_fd(),
      repository_information.staging_path.path());
  std::string destination = tmp_directory.path() + "/graveyard";

  // <base_path>/<serialization_version> becomes
  // <base_path>/<random temporary name>/graveyard/<serialization_version>
  if (renameat(repository_information.content_path.root_fd(),
               repository_information.content_path.path().c_str(),
               tmp_directory.root_fd(), destination.c_str()) != 0) {
    FXL_LOG(ERROR) << "Unable to move repository local storage to "
                   << destination << ". Error: " << strerror(errno);
    return storage::Status::IO_ERROR;
  }
  if (!files::DeletePathAt(tmp_directory.root_fd(), destination, true)) {
    FXL_LOG(ERROR) << "Unable to delete repository staging storage at "
                   << destination;
    return storage::Status::IO_ERROR;
  }
  return storage::Status::OK;
}

}  // namespace ledger
