// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>

#include <stdint.h>
#include <thread>
#include <utility>

#include <fbl/algorithm.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/devmgr-launcher/launch.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl-async/bind.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

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

fbl::RefPtr<fs::Service> MakeNode(async_dispatcher_t* dispatcher, fidl_dispatch_t* dispatch,
                                  void* ctx, const void* ops) {
    return fbl::MakeRefCounted<fs::Service>([dispatcher, dispatch, ctx, ops](zx::channel channel) {
        return fidl_bind(dispatcher, channel.release(), dispatch, ctx, ops);
    });
}

zx_status_t bootsvc_main(zx::channel bootsvc_server, GetBootItemFunction get_boot_item,
                         GetArgumentsFunction get_arguments, zx::unowned_job root_job) {
    async::Loop loop{&kAsyncLoopConfigNoAttachToThread};

    // Quit the loop when the channel is closed.
    async::Wait wait(bootsvc_server.get(), ZX_CHANNEL_PEER_CLOSED, [&loop](...) {
        loop.Quit();
    });

    // Setup VFS.
    fs::SynchronousVfs vfs(loop.dispatcher());
    auto root = fbl::MakeRefCounted<fs::PseudoDir>();

    auto items_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
    auto items_node = MakeNode(loop.dispatcher(), items_dispatch, &get_boot_item, &kItemsOps);
    root->AddEntry(fuchsia_boot_Items_Name, items_node);

    auto arguments_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Arguments_dispatch);
    auto arguments_node = MakeNode(loop.dispatcher(), arguments_dispatch, &get_arguments,
                                   &kArgumentsOps);
    root->AddEntry(fuchsia_boot_Arguments_Name, arguments_node);

    auto root_job_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_RootJob_dispatch);
    auto root_job_node = MakeNode(loop.dispatcher(), root_job_dispatch, &root_job, &kRootJobOps);
    root->AddEntry(fuchsia_boot_RootJob_Name, root_job_node);

    // Serve VFS on channel.
    auto conn = std::make_unique<fs::Connection>(&vfs, root, std::move(bootsvc_server),
                                                 ZX_FS_FLAG_DIRECTORY |
                                                 ZX_FS_RIGHT_READABLE |
                                                 ZX_FS_RIGHT_WRITABLE);
    vfs.ServeConnection(std::move(conn));

    return loop.Run();
}

} // namespace

namespace devmgr_integration_test {

devmgr_launcher::Args IsolatedDevmgr::DefaultArgs() {
    devmgr_launcher::Args args;
    args.sys_device_driver = kSysdevDriver;
    args.load_drivers.push_back("/boot/driver/test.so");
    args.driver_search_paths.push_back("/boot/driver/test");
    args.use_system_svchost = true;
    return args;
}

IsolatedDevmgr::~IsolatedDevmgr() {
    // Destroy the isolated devmgr
    if (job_.is_valid()) {
        job_.kill();
    }
}

zx_status_t IsolatedDevmgr::Create(devmgr_launcher::Args args, IsolatedDevmgr* out) {
    zx::channel bootsvc_client, bootsvc_server;
    zx_status_t status = zx::channel::create(0, &bootsvc_client, &bootsvc_server);
    if (status != ZX_OK) {
        return status;
    }

    GetBootItemFunction get_boot_item = std::move(args.get_boot_item);
    GetArgumentsFunction get_arguments = std::move(args.get_arguments);
    IsolatedDevmgr devmgr;
    zx::channel devfs;
    status = devmgr_launcher::Launch(std::move(args), std::move(bootsvc_client), &devmgr.job_,
                                     &devfs);
    if (status != ZX_OK) {
        return status;
    }

    // Launch bootsvc_main thread after calling devmgr_launcher::Launch, to
    // avoid a race when accessing devmgr.job_.
    std::thread(bootsvc_main, std::move(bootsvc_server), std::move(get_boot_item),
                std::move(get_arguments), zx::unowned_job(devmgr.job_)).detach();

    int fd;
    status = fdio_fd_create(devfs.release(), &fd);
    if (status != ZX_OK) {
        return status;
    }
    devmgr.devfs_root_.reset(fd);

    *out = std::move(devmgr);
    return ZX_OK;
}

} // namespace devmgr_integration_test
