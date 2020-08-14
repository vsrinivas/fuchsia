// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <fuchsia/exception/llcpp/fidl.h>
#include <fuchsia/scheduler/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/vfs/cpp/remote_dir.h>
#include <lib/zx/exception.h>
#include <stdint.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include <map>
#include <thread>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_printf.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <fs/vfs_types.h>
#include <mock-boot-arguments/server.h>

namespace {

using GetBootItemFunction = devmgr_launcher::GetBootItemFunction;

zx_status_t ItemsGet(void* ctx, uint32_t type, uint32_t extra, fidl_txn_t* txn) {
  const auto& get_boot_item = *static_cast<GetBootItemFunction*>(ctx);
  zx::vmo vmo;
  uint32_t length = 0;
  if (get_boot_item) {
    zx_status_t status = get_boot_item(type, extra, &vmo, &length);
    if (status != ZX_OK) {
      return status;
    }
  }
  return fuchsia_boot_ItemsGet_reply(txn, vmo.release(), length);
}

constexpr fuchsia_boot_Items_ops kItemsOps = {
    .Get = ItemsGet,
};

zx_status_t RootJobGet(void* ctx, fidl_txn_t* txn) {
  const auto& root_job = *static_cast<zx::unowned_job*>(ctx);
  zx::job job;
  zx_status_t status = root_job->duplicate(ZX_RIGHT_SAME_RIGHTS, &job);
  if (status != ZX_OK) {
    return status;
  }
  return fuchsia_boot_RootJobGet_reply(txn, job.release());
}

constexpr fuchsia_boot_RootJob_ops kRootJobOps = {
    .Get = RootJobGet,
};

void CreateFakeCppService(fbl::RefPtr<fs::PseudoDir> root, const char* name,
                          async_dispatcher_t* dispatcher,
                          std::unique_ptr<llcpp::fuchsia::boot::Arguments::Interface> server) {
  auto node = fbl::MakeRefCounted<fs::Service>(
      [dispatcher, server{std::move(server)}](zx::channel channel) {
        return fidl::BindSingleInFlightOnly(dispatcher, std::move(channel), server.get());
      });
  root->AddEntry(name, node);
}

void CreateFakeService(fbl::RefPtr<fs::PseudoDir> root, const char* name,
                       async_dispatcher_t* dispatcher, fidl_dispatch_t* dispatch, void* ctx,
                       const void* ops) {
  auto node =
      fbl::MakeRefCounted<fs::Service>([dispatcher, dispatch, ctx, ops](zx::channel channel) {
        return fidl_bind(dispatcher, channel.release(), dispatch, ctx, ops);
      });
  root->AddEntry(name, node);
}

void ForwardService(fbl::RefPtr<fs::PseudoDir> root, const char* name, zx::channel svc_client) {
  root->AddEntry(name, fbl::MakeRefCounted<fs::Service>([name, svc_client = std::move(svc_client)](
                                                            zx::channel request) {
                   return fdio_service_connect_at(svc_client.get(), name, request.release());
                 }));
}

}  // namespace

namespace devmgr_integration_test {

// We keep this structure opaque so that we don't grow a bunch of public dependencies for the
// implementation of this loop
struct IsolatedDevmgr::SvcLoopState {
  ~SvcLoopState() {
    // We must shut down the loop before we operate on vfs and bootsvc_wait in order to prevent
    // concurrent access to them
    loop.Shutdown();
  }

  GetBootItemFunction get_boot_item;

  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  fbl::RefPtr<fs::PseudoDir> root{fbl::MakeRefCounted<fs::PseudoDir>()};
  fs::SynchronousVfs vfs{loop.dispatcher()};
  async::Wait bootsvc_wait;
};

struct IsolatedDevmgr::ExceptionLoopState {
  ExceptionLoopState(async_dispatcher_t* dispatcher, zx::channel exception_channel)
      : dispatcher_(dispatcher),
        exception_channel_(std::move(exception_channel)),
        watcher_(this, exception_channel_.get(), ZX_CHANNEL_READABLE) {
    if (dispatcher_ == nullptr) {
      loop_.emplace(&kAsyncLoopConfigNoAttachToCurrentThread);
      dispatcher_ = loop_->dispatcher();
    }
    watcher_.Begin(dispatcher_);
  }
  ~ExceptionLoopState() {
    // We must shut down the loop before we operate on watcher_ in order to prevent
    // concurrent access to them. If dispatcher is passed in, this should be done beforehand.
    if (loop_) {
      loop_->Shutdown();
    }
  }

  void HandleException() {
    printf("Handling devmgr exception\n");
    zx_exception_info_t info;
    zx::exception exception;
    zx_status_t status = exception_channel_.read(0, &info, exception.reset_and_get_address(),
                                                 sizeof(info), 1, nullptr, nullptr);
    if (status != ZX_OK) {
      return;
    }

    // Send exceptions to the ambient fuchsia.exception.Handler.
    zx::channel local, remote;
    status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return;
    }
    status = fdio_service_connect(llcpp::fuchsia::exception::Handler::Name, remote.release());
    ::llcpp::fuchsia::exception::Handler::SyncClient handler(std::move(local));
    ::llcpp::fuchsia::exception::ExceptionInfo einfo;
    einfo.process_koid = info.pid;
    einfo.thread_koid = info.tid;
    einfo.type = static_cast<::llcpp::fuchsia::exception::ExceptionType>(info.type);
    handler.OnException(std::move(exception), einfo);
  }

  void DevmgrException(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
    if (status == ZX_ERR_CANCELED) {
      return;
    }
    crashed_ = true;
    HandleException();
    if (exception_callback_) {
      exception_callback_();
    }
  }

  std::optional<async::Loop> loop_;
  async_dispatcher_t* dispatcher_ = nullptr;

  zx::channel exception_channel_;
  std::atomic<bool> crashed_ = false;
  fit::closure exception_callback_;
  async::WaitMethod<ExceptionLoopState, &ExceptionLoopState::DevmgrException> watcher_;
};

zx_status_t IsolatedDevmgr::SetupExceptionLoop(async_dispatcher_t* dispatcher,
                                               zx::channel exception_channel) {
  exception_loop_state_ =
      std::make_unique<ExceptionLoopState>(dispatcher, std::move(exception_channel));

  if (dispatcher == nullptr) {
    return exception_loop_state_->loop_->StartThread("isolated-devmgr-exceptionloop");
  } else {
    return ZX_OK;
  }
}

// Create and host a /svc directory for the devcoordinator process we're creating.
// TODO(fxbug.dev/35991): IsolatedDevmgr and devmgr_launcher should be rewritten to make use of
// Components v2/Test Framework concepts as soon as those are ready enough. For now this has to be
// manually kept in sync with devcoordinator's manifest in //src/sys/root/devcoordinator.cml
// (although it already seems to be incomplete).
zx_status_t IsolatedDevmgr::SetupSvcLoop(zx::channel bootsvc_server,
                                         zx::channel fshost_outgoing_client,
                                         GetBootItemFunction get_boot_item,
                                         std::map<std::string, std::string>&& boot_args) {
  svc_loop_state_ = std::make_unique<SvcLoopState>();
  svc_loop_state_->get_boot_item = std::move(get_boot_item);

  // Quit the loop when the channel is closed.
  svc_loop_state_->bootsvc_wait.set_object(bootsvc_server.get());
  svc_loop_state_->bootsvc_wait.set_trigger(ZX_CHANNEL_PEER_CLOSED);
  svc_loop_state_->bootsvc_wait.set_handler([loop = &svc_loop_state_->loop](...) { loop->Quit(); });
  svc_loop_state_->bootsvc_wait.Begin(svc_loop_state_->loop.dispatcher());

  // Connect to /svc in the current namespace.
  zx::channel svc_client;
  {
    zx::channel svc_server;
    zx_status_t status = zx::channel::create(0, &svc_client, &svc_server);
    if (status != ZX_OK) {
      return status;
    }
    status = fdio_service_connect("/svc", svc_server.release());

    if (status != ZX_OK) {
      return status;
    }
  }

  // Connect to /svc in fshost's outgoing directory
  zx::channel fshost_svc_client;
  {
    zx::channel fshost_svc_server;
    zx_status_t status = zx::channel::create(0, &fshost_svc_client, &fshost_svc_server);
    if (status != ZX_OK) {
      return status;
    }
    status = fdio_open_at(fshost_outgoing_client.get(), "svc",
                          ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_FLAG_DIRECTORY,
                          fshost_svc_server.release());
    if (status != ZX_OK) {
      return status;
    }
  }

  // Forward required services from the current namespace.
  zx::channel svc_client2(fdio_service_clone(svc_client.get()));
  zx::channel svc_client3(fdio_service_clone(svc_client.get()));
  ForwardService(svc_loop_state_->root, "fuchsia.process.Launcher", std::move(svc_client));
  ForwardService(svc_loop_state_->root, "fuchsia.logger.LogSink", std::move(svc_client2));
  ForwardService(svc_loop_state_->root, "fuchsia.boot.RootResource", std::move(svc_client3));
  ForwardService(svc_loop_state_->root, "fuchsia.fshost.Loader", std::move(fshost_svc_client));

  boot_args.try_emplace("virtcon.disable", "true");

  // Host fake instances of some services normally provided by bootsvc and routed to devcoordinator
  // by component_manager. The difference between these fakes and the optional services above is
  // that these 1) are fakeable (unlike fuchsia.process.Launcher) and 2) seem to be required
  // services for devcoordinator.
  auto items_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
  CreateFakeService(svc_loop_state_->root, fuchsia_boot_Items_Name,
                    svc_loop_state_->loop.dispatcher(), items_dispatch,
                    &svc_loop_state_->get_boot_item, &kItemsOps);

  auto root_job_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_RootJob_dispatch);
  CreateFakeService(svc_loop_state_->root, fuchsia_boot_RootJob_Name,
                    svc_loop_state_->loop.dispatcher(), root_job_dispatch, &job_, &kRootJobOps);

  CreateFakeCppService(svc_loop_state_->root, llcpp::fuchsia::boot::Arguments::Name,
                       svc_loop_state_->loop.dispatcher(),
                       std::make_unique<mock_boot_arguments::Server>(std::move(boot_args)));

  // Serve VFS on channel.
  svc_loop_state_->vfs.ServeDirectory(svc_loop_state_->root, std::move(bootsvc_server),
                                      fs::Rights::ReadWrite());

  return svc_loop_state_->loop.StartThread("isolated-devmgr-svcloop");
}

__EXPORT
zx_status_t IsolatedDevmgr::AddDevfsToOutgoingDir(vfs::PseudoDir* outgoing_root_dir,
                                                  async_dispatcher_t* dispatcher) {
  zx::channel client, server;
  auto status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  fdio_cpp::UnownedFdioCaller fd(devfs_root_.get());
  fdio_service_clone_to(fd.borrow_channel(), server.release());

  // Add devfs to out directory.
  auto devfs_out = std::make_unique<vfs::RemoteDir>(std::move(client), dispatcher);
  outgoing_root_dir->AddEntry("dev", std::move(devfs_out));
  return ZX_OK;
}

__EXPORT
devmgr_launcher::Args IsolatedDevmgr::DefaultArgs() {
  devmgr_launcher::Args args;
  args.sys_device_driver = kSysdevDriver;
  args.load_drivers.push_back("/boot/driver/test.so");
  args.driver_search_paths.push_back("/boot/driver/test");
  return args;
}

__EXPORT
IsolatedDevmgr::IsolatedDevmgr() = default;

__EXPORT
IsolatedDevmgr::~IsolatedDevmgr() { Terminate(); }

__EXPORT
IsolatedDevmgr::IsolatedDevmgr(IsolatedDevmgr&& other)
    : job_(std::move(other.job_)),
      svc_root_dir_(std::move(other.svc_root_dir_)),
      fshost_outgoing_dir_(std::move(other.fshost_outgoing_dir_)),
      devfs_root_(std::move(other.devfs_root_)),
      svc_loop_state_(std::move(other.svc_loop_state_)),
      exception_loop_state_(std::move(other.exception_loop_state_)) {}

__EXPORT
IsolatedDevmgr& IsolatedDevmgr::operator=(IsolatedDevmgr&& other) {
  Terminate();
  job_ = std::move(other.job_);
  devfs_root_ = std::move(other.devfs_root_);
  svc_root_dir_ = std::move(other.svc_root_dir_);
  fshost_outgoing_dir_ = std::move(other.fshost_outgoing_dir_);
  svc_loop_state_ = std::move(other.svc_loop_state_);
  exception_loop_state_ = std::move(other.exception_loop_state_);
  return *this;
}

__EXPORT
void IsolatedDevmgr::Terminate() {
  if (job_.is_valid()) {
    job_.kill();

    // Best-effort; ignores error.
    zx_signals_t observed = 0;
    job_.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), &observed);
  }
  job_.reset();
}

__EXPORT
zx_status_t IsolatedDevmgr::Create(devmgr_launcher::Args args, IsolatedDevmgr* out) {
  return Create(std::move(args), nullptr, out);
}

__EXPORT
zx_status_t IsolatedDevmgr::Create(devmgr_launcher::Args args, async_dispatcher_t* dispatcher,
                                   IsolatedDevmgr* out) {
  zx::channel svc_client, svc_server;
  zx_status_t status = zx::channel::create(0, &svc_client, &svc_server);
  if (status != ZX_OK) {
    return status;
  }

  zx::channel fshost_outgoing_client, fshost_outgoing_server;
  status = zx::channel::create(0, &fshost_outgoing_client, &fshost_outgoing_server);
  if (status != ZX_OK) {
    return status;
  }

  GetBootItemFunction get_boot_item = std::move(args.get_boot_item);
  zx::channel component_lifecycle_client, component_lifecycle_server;
  status = zx::channel::create(0, &component_lifecycle_client, &component_lifecycle_server);
  if (status != ZX_OK) {
    return status;
  }
  IsolatedDevmgr devmgr;
  zx::channel devfs;
  zx::channel outgoing_svc_root;
  std::map<std::string, std::string> boot_args = std::move(args.boot_args);
  status = devmgr_launcher::Launch(
      std::move(args), std::move(svc_client), std::move(fshost_outgoing_server),
      std::move(component_lifecycle_server), &devmgr.job_, &devfs, &outgoing_svc_root);
  if (status != ZX_OK) {
    return status;
  }

  zx::channel exception_channel;
  devmgr.containing_job().create_exception_channel(0, &exception_channel);

  status = devmgr.SetupExceptionLoop(dispatcher, std::move(exception_channel));
  if (status != ZX_OK) {
    return status;
  }

  zx::channel fshost_client_dup(fdio_service_clone(fshost_outgoing_client.get()));
  status = devmgr.SetupSvcLoop(std::move(svc_server), std::move(fshost_outgoing_client),
                               std::move(get_boot_item), std::move(boot_args));
  if (status != ZX_OK) {
    return status;
  }

  int fd;
  status = fdio_fd_create(devfs.release(), &fd);
  if (status != ZX_OK) {
    return status;
  }
  devmgr.devfs_root_.reset(fd);
  devmgr.component_lifecycle_client_.reset(component_lifecycle_client.release());
  devmgr.svc_root_dir_.reset(outgoing_svc_root.release());
  devmgr.fshost_outgoing_dir_.reset(fshost_client_dup.release());
  *out = std::move(devmgr);
  return ZX_OK;
}

__EXPORT void IsolatedDevmgr::SetExceptionCallback(fit::closure exception_callback) {
  exception_loop_state_->exception_callback_ = std::move(exception_callback);
}

__EXPORT bool IsolatedDevmgr::crashed() const { return exception_loop_state_->crashed_; }

}  // namespace devmgr_integration_test
