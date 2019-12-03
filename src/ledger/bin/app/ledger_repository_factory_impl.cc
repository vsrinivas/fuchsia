// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/ledger_repository_factory_impl.h"

#include <fcntl.h>
#include <lib/async/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/function.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <memory>

#include <trace/event.h>

#include "src/ledger/bin/app/background_sync_manager.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/app/db_view_factory.h"
#include "src/ledger/bin/app/disk_cleanup_manager_impl.h"
#include "src/ledger/bin/app/serialization.h"
#include "src/ledger/bin/app/serialization_version.h"
#include "src/ledger/bin/clocks/impl/device_id_manager_impl.h"
#include "src/ledger/bin/clocks/public/device_fingerprint_manager.h"
#include "src/ledger/bin/cloud_sync/impl/user_sync_impl.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/p2p_provider/impl/p2p_provider_impl.h"
#include "src/ledger/bin/p2p_provider/impl/static_user_id_provider.h"
#include "src/ledger/bin/p2p_sync/impl/user_communicator_impl.h"
#include "src/ledger/bin/storage/impl/leveldb_factory.h"
#include "src/ledger/bin/sync_coordinator/impl/user_sync_impl.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "src/lib/callback/scoped_callback.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/eintr_wrapper.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/lib/fxl/strings/string_view.h"
#include "src/lib/inspect_deprecated/deprecated/expose.h"
#include "src/lib/inspect_deprecated/deprecated/object_dir.h"
#include "src/lib/inspect_deprecated/inspect.h"

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

bool GetRepositoryName(rng::Random* random, const DetachedPath& content_path, std::string* name) {
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
  if (!files::WriteFileAt(name_path.root_fd(), name_path.path(), new_name.c_str(),
                          new_name.size())) {
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
  explicit LedgerRepositoryContainer(std::shared_ptr<fbl::unique_fd> root_fd)
      : root_fd_(std::move(root_fd)) {
    // Ensure that we close the repository if the underlying filesystem closes
    // too. This prevents us from trying to write on disk when there's no disk
    // anymore. This situation can happen when the Ledger is shut down, if the
    // storage is shut down at the same time.
    fd_chan_ = fsl::CloneChannelFromFileDescriptor(root_fd_->get());
    fd_wait_ = std::make_unique<async::Wait>(
        fd_chan_.get(), ZX_CHANNEL_PEER_CLOSED, 0,
        [](async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
           const zx_packet_signal* signal) {
          FXL_CHECK(false) << "Ledger file system has been closed while Ledger is running.";
        });
    zx_status_t status = fd_wait_->Begin(async_get_default_dispatcher());
    FXL_DCHECK(status == ZX_OK);
  }

  LedgerRepositoryContainer(const LedgerRepositoryContainer&) = delete;
  LedgerRepositoryContainer& operator=(const LedgerRepositoryContainer&) = delete;

  ~LedgerRepositoryContainer() {
    for (const auto& request : requests_) {
      request.second(Status::INTERNAL_ERROR);
    }
  }

  void SetOnDiscardable(fit::closure on_discardable) {
    on_discardable_ = std::move(on_discardable);
  };

  bool IsDiscardable() const {
    return !fd_wait_->is_pending() || !ledger_repository_ || ledger_repository_->IsDiscardable();
  }

  // Keeps track of |request| and |callback|. Binds |request| and fires
  // |callback| when the repository is available or an error occurs.
  void BindRepository(fidl::InterfaceRequest<ledger_internal::LedgerRepository> request,
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
  void SetRepository(Status status, std::unique_ptr<LedgerRepositoryImpl> ledger_repository) {
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
    if (ledger_repository_) {
      ledger_repository_->SetOnDiscardable([this] { OnDiscardable(); });
    } else {
      OnDiscardable();
    }
  }

 private:
  void OnDiscardable() const {
    if (on_discardable_) {
      on_discardable_();
    }
  }

  std::shared_ptr<fbl::unique_fd> root_fd_;
  zx::channel fd_chan_;
  std::unique_ptr<async::Wait> fd_wait_;
  // This callback is invoked indirectly when ledger_repository_ is destructed, because the
  // on_discardable callback of ledger_repository_ is set (in |SetRepository|) to invoke
  // |LedgerRepositoryContainer::OnDiscardable|. Therefore, on_discardable_ must outlive
  // ledger_repository_.
  fit::closure on_discardable_;
  std::unique_ptr<LedgerRepositoryImpl> ledger_repository_;
  Status status_ = Status::OK;
  std::vector<std::pair<fidl::InterfaceRequest<ledger_internal::LedgerRepository>,
                        fit::function<void(Status)>>>
      requests_;
  std::vector<fidl::InterfaceRequest<ledger_internal::LedgerRepository>> detached_handles_;
};

struct LedgerRepositoryFactoryImpl::RepositoryInformation {
 public:
  explicit RepositoryInformation(std::shared_ptr<fbl::unique_fd> root_fd, std::string user_id)
      : root_fd_(std::move(root_fd)),
        base_path(root_fd_->get()),
        content_path(base_path.SubPath(kSerializationVersion)),
        cache_path(content_path.SubPath(kCachePath)),
        page_usage_db_path(content_path.SubPath(kPageUsageDbPath)),
        ledgers_path(content_path.SubPath(kLedgersPath)),
        staging_path(base_path.SubPath(kStagingPath)),
        user_id(std::move(user_id)) {}

  RepositoryInformation(const RepositoryInformation& other) = default;
  RepositoryInformation(RepositoryInformation&& other) = default;

  bool Init(rng::Random* random) { return GetRepositoryName(random, content_path, &name); }

 private:
  std::shared_ptr<fbl::unique_fd> root_fd_;

 public:
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
    std::unique_ptr<p2p_sync::UserCommunicatorFactory> user_communicator_factory,
    inspect_deprecated::Node inspect_node)
    : environment_(environment),
      user_communicator_factory_(std::move(user_communicator_factory)),
      repositories_(environment_->dispatcher()),
      inspect_node_(std::move(inspect_node)),
      coroutine_manager_(environment_->coroutine_service()),
      weak_factory_(this) {}

LedgerRepositoryFactoryImpl::~LedgerRepositoryFactoryImpl() = default;

void LedgerRepositoryFactoryImpl::GetRepository(
    zx::channel repository_handle,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider, std::string user_id,
    fidl::InterfaceRequest<ledger_internal::LedgerRepository> repository_request,
    fit::function<void(Status)> callback) {
  fbl::unique_fd root_fd = fsl::OpenChannelAsFileDescriptor(std::move(repository_handle));
  if (!root_fd.is_valid()) {
    callback(Status::IO_ERROR);
    return;
  }
  GetRepositoryByFD(std::make_shared<fbl::unique_fd>(std::move(root_fd)), std::move(cloud_provider),
                    user_id, std::move(repository_request), std::move(callback));
}

void LedgerRepositoryFactoryImpl::GetRepositoryByFD(
    std::shared_ptr<fbl::unique_fd> root_fd,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider, std::string user_id,
    fidl::InterfaceRequest<ledger_internal::LedgerRepository> repository_request,
    fit::function<void(Status)> callback) {
  TRACE_DURATION("ledger", "repository_factory_get_repository");

  RepositoryInformation repository_information(root_fd, std::move(user_id));
  if (!repository_information.Init(environment_->random())) {
    callback(Status::IO_ERROR);
    return;
  }

  auto it = repositories_.find(repository_information.name);
  if (it != repositories_.end()) {
    it->second.BindRepository(std::move(repository_request), std::move(callback));
    return;
  }

  auto ret = repositories_.try_emplace(repository_information.name, std::move(root_fd));
  LedgerRepositoryContainer* container = &ret.first->second;
  container->BindRepository(std::move(repository_request), std::move(callback));
  coroutine_manager_.StartCoroutine([this,
                                     repository_information = std::move(repository_information),
                                     cloud_provider = std::move(cloud_provider),
                                     container](coroutine::CoroutineHandler* handler) mutable {
    std::unique_ptr<LedgerRepositoryImpl> repository;
    Status status = SynchronousCreateLedgerRepository(
        handler, std::move(cloud_provider), std::move(repository_information), &repository);

    container->SetRepository(status, std::move(repository));
  });
}

Status LedgerRepositoryFactoryImpl::SynchronousCreateLedgerRepository(
    coroutine::CoroutineHandler* handler,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
    RepositoryInformation repository_information,
    std::unique_ptr<LedgerRepositoryImpl>* repository) {
  auto db_factory =
      std::make_unique<storage::LevelDbFactory>(environment_, repository_information.cache_path);
  db_factory->Init();
  auto db_path =
      repository_information.page_usage_db_path.SubPath(kRepositoryDbSerializationVersion);
  if (!files::CreateDirectoryAt(db_path.root_fd(), db_path.path())) {
    return Status::IO_ERROR;
  }
  std::unique_ptr<storage::Db> base_db;
  Status status;
  if (coroutine::SyncCall(
          handler,
          [db_factory_ptr = db_factory.get(), db_path = std::move(db_path)](
              fit::function<void(Status, std::unique_ptr<storage::Db>)> callback) mutable {
            db_factory_ptr->GetOrCreateDb(
                std::move(db_path), storage::DbFactory::OnDbNotFound::CREATE, std::move(callback));
          },
          &status, &base_db) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  RETURN_ON_ERROR(status);

  auto dbview_factory = std::make_unique<DbViewFactory>(std::move(base_db));

  auto device_id_manager = std::make_unique<clocks::DeviceIdManagerImpl>(
      environment_, dbview_factory->CreateDbView(RepositoryRowPrefix::CLOCKS));
  RETURN_ON_ERROR(device_id_manager->Init(handler));

  auto page_usage_db = std::make_unique<PageUsageDb>(
      environment_, dbview_factory->CreateDbView(RepositoryRowPrefix::PAGE_USAGE_DB));

  auto disk_cleanup_manager =
      std::make_unique<DiskCleanupManagerImpl>(environment_, page_usage_db.get());
  auto background_sync_manager =
      std::make_unique<BackgroundSyncManager>(environment_, page_usage_db.get());

  std::unique_ptr<SyncWatcherSet> watchers =
      std::make_unique<SyncWatcherSet>(environment_->dispatcher());
  std::unique_ptr<sync_coordinator::UserSyncImpl> user_sync = CreateUserSync(
      repository_information, std::move(cloud_provider), watchers.get(), device_id_manager.get());
  if (!user_sync) {
    FXL_LOG(WARNING) << "No cloud provider nor P2P communicator - Ledger will work locally but "
                     << "not sync. (running in Guest mode?)";
  }

  DiskCleanupManagerImpl* disk_cleanup_manager_ptr = disk_cleanup_manager.get();
  BackgroundSyncManager* background_sync_manager_ptr = background_sync_manager.get();
  *repository = std::make_unique<LedgerRepositoryImpl>(
      repository_information.ledgers_path, environment_, std::move(db_factory),
      std::move(dbview_factory), std::move(page_usage_db), std::move(watchers),
      std::move(user_sync), std::move(disk_cleanup_manager), std::move(background_sync_manager),
      std::vector<PageUsageListener*>{disk_cleanup_manager_ptr, background_sync_manager_ptr},
      std::move(device_id_manager),
      inspect_node_.CreateChild(convert::ToHex(repository_information.name)));
  disk_cleanup_manager_ptr->SetPageEvictionDelegate(repository->get());
  background_sync_manager_ptr->SetDelegate(repository->get());
  return Status::OK;
}

std::unique_ptr<sync_coordinator::UserSyncImpl> LedgerRepositoryFactoryImpl::CreateUserSync(
    const RepositoryInformation& repository_information,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider, SyncWatcherSet* watchers,
    clocks::DeviceFingerprintManager* fingerprint_manager) {
  std::unique_ptr<cloud_sync::UserSyncImpl> cloud_sync =
      CreateCloudSync(repository_information, std::move(cloud_provider), fingerprint_manager);
  std::unique_ptr<p2p_sync::UserCommunicator> p2p_sync = CreateP2PSync(repository_information);

  if (!cloud_sync && !p2p_sync) {
    return nullptr;
  }

  auto user_sync =
      std::make_unique<sync_coordinator::UserSyncImpl>(std::move(cloud_sync), std::move(p2p_sync));
  user_sync->SetWatcher(watchers);
  user_sync->Start();
  return user_sync;
}

std::unique_ptr<cloud_sync::UserSyncImpl> LedgerRepositoryFactoryImpl::CreateCloudSync(
    const RepositoryInformation& repository_information,
    fidl::InterfaceHandle<cloud_provider::CloudProvider> cloud_provider,
    clocks::DeviceFingerprintManager* fingerprint_manager) {
  if (!cloud_provider) {
    return nullptr;
  }

  auto cloud_provider_ptr = cloud_provider.Bind();
  cloud_provider_ptr.set_error_handler([](zx_status_t status) {
    FXL_LOG(ERROR) << "Lost connection to cloud provider; cloud sync will no longer work.";
  });

  cloud_sync::UserConfig user_config;
  user_config.user_directory = repository_information.content_path;
  user_config.cloud_provider = std::move(cloud_provider_ptr);
  fit::closure on_version_mismatch = [this, repository_information]() mutable {
    OnVersionMismatch(repository_information);
  };
  return std::make_unique<cloud_sync::UserSyncImpl>(
      environment_, std::move(user_config), environment_->MakeBackoff(),
      std::move(on_version_mismatch), fingerprint_manager);
}

std::unique_ptr<p2p_sync::UserCommunicator> LedgerRepositoryFactoryImpl::CreateP2PSync(
    const RepositoryInformation& repository_information) {
  if (!user_communicator_factory_) {
    return nullptr;
  }

  if (repository_information.user_id.empty()) {
    return nullptr;
  }

  auto user_id_provider =
      std::make_unique<p2p_provider::StaticUserIdProvider>(repository_information.user_id);

  return user_communicator_factory_->GetUserCommunicator(std::move(user_id_provider));
}

void LedgerRepositoryFactoryImpl::OnVersionMismatch(RepositoryInformation repository_information) {
  FXL_LOG(WARNING) << "Data in the cloud was wiped out, erasing local state. "
                   << "This should log you out, log back in to start syncing again.";

  // First, shut down the repository so that we can delete the files while it's
  // not running.
  auto find_repository = repositories_.find(repository_information.name);
  FXL_DCHECK(find_repository != repositories_.end());
  repositories_.erase(find_repository);
  DeleteRepositoryDirectory(repository_information);
}

void LedgerRepositoryFactoryImpl::DeleteRepositoryDirectory(
    const RepositoryInformation& repository_information) {
  files::ScopedTempDirAt tmp_directory(repository_information.staging_path.root_fd(),
                                       repository_information.staging_path.path());
  std::string destination = tmp_directory.path() + "/graveyard";

  // <base_path>/<serialization_version> becomes
  // <base_path>/<random temporary name>/graveyard/<serialization_version>
  if (renameat(repository_information.content_path.root_fd(),
               repository_information.content_path.path().c_str(), tmp_directory.root_fd(),
               destination.c_str()) != 0) {
    FXL_LOG(ERROR) << "Unable to move repository local storage to " << destination
                   << ". Error: " << strerror(errno);
    return;
  }
  if (!files::DeletePathAt(tmp_directory.root_fd(), destination, true)) {
    FXL_LOG(ERROR) << "Unable to delete repository staging storage at " << destination;
    return;
  }
}

}  // namespace ledger
