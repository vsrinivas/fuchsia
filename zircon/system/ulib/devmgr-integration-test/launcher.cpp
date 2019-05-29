// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>

#include <stdint.h>
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

struct BootsvcData {
    zx::channel bootsvc_server;
    zx::job* root_job;
    devmgr_launcher::GetBootItemFunction get_boot_item;
};

zx_status_t ItemsGet(void* ctx, uint32_t type, uint32_t extra, fidl_txn_t* txn) {
    auto data = static_cast<BootsvcData*>(ctx);
    zx::vmo vmo;
    uint32_t length = 0;
    if (data->get_boot_item) {
        zx_status_t status = data->get_boot_item(type, extra, &vmo, &length);
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
    auto* data = static_cast<BootsvcData*>(ctx);
    zx::job root_job;
    zx_status_t status = data->root_job->duplicate(ZX_RIGHT_SAME_RIGHTS, &root_job);
    if (status != ZX_OK) {
        return status;
    }
    return fuchsia_boot_RootJobGet_reply(txn, root_job.release());
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

int bootsvc_main(void* arg) {
    auto data = std::unique_ptr<BootsvcData>(static_cast<BootsvcData*>(arg));
    async::Loop loop{&kAsyncLoopConfigNoAttachToThread};

    // Quit the loop when the channel is closed.
    async::Wait wait(data->bootsvc_server.get(), ZX_CHANNEL_PEER_CLOSED, [&loop](...) {
        loop.Quit();
    });

    // Setup VFS.
    fs::SynchronousVfs vfs(loop.dispatcher());
    auto root = fbl::MakeRefCounted<fs::PseudoDir>();
    auto items_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
    auto items_node = MakeNode(loop.dispatcher(), items_dispatch, data.get(), &kItemsOps);
    root->AddEntry(fuchsia_boot_Items_Name, items_node);
    auto root_job_dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_RootJob_dispatch);
    auto root_job_node = MakeNode(loop.dispatcher(), root_job_dispatch, data.get(), &kRootJobOps);
    root->AddEntry(fuchsia_boot_RootJob_Name, root_job_node);

    // Serve VFS on channel.
    auto conn = std::make_unique<fs::Connection>(&vfs, root, std::move(data->bootsvc_server),
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
    IsolatedDevmgr devmgr;

    auto data = std::make_unique<BootsvcData>();
    zx::channel bootsvc_client;
    zx_status_t status = zx::channel::create(0, &bootsvc_client, &data->bootsvc_server);
    if (status != ZX_OK) {
        return status;
    }
    data->root_job = &devmgr.job_;
    data->get_boot_item = std::move(args.get_boot_item);

    thrd_t t;
    int ret = thrd_create_with_name(&t, bootsvc_main, data.release(), "bootsvc");
    if (ret != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    thrd_detach(t);

    zx::channel devfs;
    status = devmgr_launcher::Launch(std::move(args), std::move(bootsvc_client), &devmgr.job_,
                                     &devfs);
    if (status != ZX_OK) {
        return status;
    }

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
