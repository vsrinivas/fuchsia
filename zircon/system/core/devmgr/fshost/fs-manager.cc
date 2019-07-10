// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fs/vfs.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/zircon-internal/debug.h>
#include <lib/fdio/io.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <utility>

#include "fs-manager.h"

#define ZXDEBUG 0

namespace devmgr {

// Must appear in the devmgr namespace.
// Expected dependency of "shared/fdio.h".
//
// This is currently exposed in a somewhat odd location to also be
// visible to unit tests, avoiding linkage errors.
zx::channel fs_clone(const char* path) {
    if (strcmp(path, "svc") == 0) {
        path = "/svc";
    } else if (strcmp(path, "data") == 0) {
        path = "/fs/data";
    } else if (strcmp(path, "blob") == 0) {
        path = "/fs/blob";
    } else {
        printf("%s: Cannot clone: %s\n", __FUNCTION__, path);
        return zx::channel();
    }

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    if (status != ZX_OK) {
        return zx::channel();
    }
    status = fdio_service_connect(path, server.release());
    if (status != ZX_OK) {
        printf("%s: Failed to connect to %s: %d\n", __FUNCTION__, path, status);
        return zx::channel();
    }
    return client;
}

FsManager::FsManager(zx::event fshost_event)
    : event_(std::move(fshost_event)),
      global_loop_(new async::Loop(&kAsyncLoopConfigNoAttachToThread)),
      registry_(global_loop_.get()) {
    ZX_ASSERT(global_root_ == nullptr);
}

// In the event that we haven't been explicitly signalled, tear ourself down.
FsManager::~FsManager() {
    if (global_shutdown_.has_handler()) {
        event_.signal(0, FSHOST_SIGNAL_EXIT);
        auto deadline = zx::deadline_after(zx::sec(2));
        zx_signals_t pending;
        event_.wait_one(FSHOST_SIGNAL_EXIT_DONE, deadline, &pending);
    }
}

zx_status_t FsManager::Create(zx::event fshost_event, std::unique_ptr<FsManager>* out) {
    auto fs_manager = std::unique_ptr<FsManager>(new FsManager(std::move(fshost_event)));
    zx_status_t status = fs_manager->Initialize();
    if (status != ZX_OK) {
        return status;
    }
    *out = std::move(fs_manager);
    return ZX_OK;
}

zx_status_t FsManager::Initialize() {
    uint64_t physmem_size = zx_system_get_physmem();
    ZX_DEBUG_ASSERT(physmem_size % PAGE_SIZE == 0);
    size_t page_limit = physmem_size / PAGE_SIZE;

    zx_status_t status = memfs::Vfs::Create("<root>", page_limit, &root_vfs_, &global_root_);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<fs::Vnode> vn;
    if ((status = global_root_->Create(&vn, "boot", S_IFDIR)) != ZX_OK) {
        return status;
    }
    if ((status = global_root_->Create(&vn, "tmp", S_IFDIR)) != ZX_OK) {
        return status;
    }
    for (unsigned n = 0; n < fbl::count_of(kMountPoints); n++) {
        fbl::StringPiece pathout;
        status = root_vfs_->Open(global_root_, &mount_nodes[n], fbl::StringPiece(kMountPoints[n]),
                                 &pathout,
                                 ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_FLAG_CREATE,
                                 S_IFDIR);
        if (status != ZX_OK) {
            return status;
        }
    }

    global_loop_->StartThread("root-dispatcher");
    root_vfs_->SetDispatcher(global_loop_->dispatcher());
    return ZX_OK;
}

zx_status_t FsManager::InstallFs(const char* path, zx::channel h) {
    for (unsigned n = 0; n < fbl::count_of(kMountPoints); n++) {
        if (!strcmp(path, kMountPoints[n])) {
            return root_vfs_->InstallRemote(mount_nodes[n], fs::MountChannel(std::move(h)));
        }
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t FsManager::ServeRoot(zx::channel server) {
    return root_vfs_->ServeDirectory(global_root_, std::move(server), ZX_FS_RIGHTS);
}

void FsManager::WatchExit() {
    global_shutdown_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
        root_vfs_->UninstallAll(ZX_TIME_INFINITE);
        event_.signal(0, FSHOST_SIGNAL_EXIT_DONE);
    });

    global_shutdown_.set_object(event_.get());
    global_shutdown_.set_trigger(FSHOST_SIGNAL_EXIT);
    global_shutdown_.Begin(global_loop_->dispatcher());
}

} // namespace devmgr
