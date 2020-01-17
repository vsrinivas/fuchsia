// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <fuchsia/process/c/fidl.h>
#include <fuchsia/scheduler/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl-async/bind.h>
#include <stdint.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <thread>
#include <utility>

#include <fbl/algorithm.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <fs/vfs_types.h>

#include "fbl/ref_ptr.h"

namespace {

using GetBootItemFunction = devmgr_launcher::GetBootItemFunction;
using GetArgumentsFunction = devmgr_launcher::GetArgumentsFunction;

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

zx_status_t ArgumentsGet(void* ctx, fidl_txn_t* txn) {
  const auto& get_arguments = *static_cast<GetArgumentsFunction*>(ctx);
  zx::vmo vmo;
  uint32_t length = 0;
  if (get_arguments) {
    zx_status_t status = get_arguments(&vmo, &length);
    if (status != ZX_OK) {
      return status;
    }
  }
  return fuchsia_boot_ArgumentsGet_reply(txn, vmo.release(), length);
}

constexpr fuchsia_boot_Arguments_ops kArgumentsOps = {
    .Get = ArgumentsGet,
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

void CreateFakeService(fbl::RefPtr<fs::PseudoDir> root, const char* name,
                       async_dispatcher_t* dispatcher, fidl_dispatch_t* dispatch, void* ctx,
                       const void* ops) {
  auto node =
      fbl::MakeRefCounted<fs::Service>([dispatcher, dispatch, ctx, ops](zx::channel channel) {
        return fidl_bind(dispatcher, channel.release(), dispatch, ctx, ops);
      });
  root->AddEntry(name, node);
}

void ForwardService(fbl::RefPtr<fs::PseudoDir> root, const char* name,
                    zx::unowned_channel svc_client) {
  root->AddEntry(name, fbl::MakeRefCounted<fs::Service>([name, svc_client = std::move(svc_client)](
                                                            zx::channel request) {
                   return fdio_service_connect_at(svc_client->get(), name, request.release());
                 }));
}

// Create and host a /svc directory for the devcoordinator process we're creating.
// TODO(fxb/35991): IsolatedDevmgr and devmgr_launcher should be rewritten to make use of
// Components v2/Test Framework concepts as soon as those are ready enough. For now this has to be
// manually kept in sync with devcoordinator's manifest in //src/sys/root/devcoordinator.cml
// (although it already seems to be incomplete).
zx_status_t host_svc_directory(zx::channel bootsvc_server, zx::channel fshost_outgoing_client,
                               GetBootItemFunction get_boot_item,
                               GetArgumentsFunction get_arguments, zx::unowned_job root_job) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Quit the loop when the channel is closed.
  async::Wait wait(bootsvc_server.get(), ZX_CHANNEL_PEER_CLOSED, 0, [&loop](...) { loop.Quit(); });
  wait.Begin(loop.dispatcher());

  // Setup VFS.
  fs::SynchronousVfs vfs(loop.dispatcher());
  auto root = fbl::MakeRefCounted<fs::PseudoDir>();

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

  // Forward required services from the current namespace. Currently this is just
  // fuchsia.process.Launcher.
  ForwardService(root, fuchsia_process_Launcher_Name, zx::unowned_channel(svc_client));
  ForwardService(root, "fuchsia.fshost.Loader", zx::unowned_channel(fshost_svc_client));

  // Host fake instances of some services normally provided by bootsvc and routed to devcoordinator
  // by component_manager. The difference between these fakes and the optional services above is
  // that these 1) are fakeable (unlike fuchsia.process.Launcher) and 2) seem to be required
  // services for devcoordinator.
  auto items_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
  CreateFakeService(root, fuchsia_boot_Items_Name, loop.dispatcher(), items_dispatch,
                    &get_boot_item, &kItemsOps);

  auto arguments_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Arguments_dispatch);
  CreateFakeService(root, fuchsia_boot_Arguments_Name, loop.dispatcher(), arguments_dispatch,
                    &get_arguments, &kArgumentsOps);

  auto root_job_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_RootJob_dispatch);
  CreateFakeService(root, fuchsia_boot_RootJob_Name, loop.dispatcher(), root_job_dispatch,
                    &root_job, &kRootJobOps);

  // Serve VFS on channel.
  vfs.ServeDirectory(root, std::move(bootsvc_server), fs::Rights::ReadWrite());

  return loop.Run();
}

}  // namespace

namespace devmgr_integration_test {

__EXPORT
devmgr_launcher::Args IsolatedDevmgr::DefaultArgs() {
  devmgr_launcher::Args args;
  args.sys_device_driver = kSysdevDriver;
  args.load_drivers.push_back("/boot/driver/test.so");
  args.driver_search_paths.push_back("/boot/driver/test");
  return args;
}

__EXPORT
IsolatedDevmgr::~IsolatedDevmgr() { Terminate(); }

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
  GetArgumentsFunction get_arguments = std::move(args.get_arguments);

  IsolatedDevmgr devmgr;
  zx::channel devfs;
  zx::channel outgoing_svc_root;
  status = devmgr_launcher::Launch(std::move(args), std::move(svc_client),
                                   std::move(fshost_outgoing_server), &devmgr.job_, &devfs,
                                   &outgoing_svc_root);
  if (status != ZX_OK) {
    return status;
  }

  // Launch host_svc_directory thread after calling devmgr_launcher::Launch, to
  // avoid a race when accessing devmgr.job_.
  std::thread(host_svc_directory, std::move(svc_server), std::move(fshost_outgoing_client),
              std::move(get_boot_item), std::move(get_arguments), zx::unowned_job(devmgr.job_))
      .detach();

  int fd;
  status = fdio_fd_create(devfs.release(), &fd);
  if (status != ZX_OK) {
    return status;
  }
  devmgr.devfs_root_.reset(fd);

  devmgr.svc_root_dir_.reset(outgoing_svc_root.release());
  *out = std::move(devmgr);
  return ZX_OK;
}

}  // namespace devmgr_integration_test
