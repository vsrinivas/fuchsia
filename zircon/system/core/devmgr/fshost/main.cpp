// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>

#include <bootdata/decompress.h>
#include <fbl/unique_fd.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <loader-service/loader-service.h>
#include <ramdevice-client/ramdisk.h>
#include <zircon/device/vfs.h>
#include <zircon/dlfcn.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include "fshost.h"

namespace devmgr {
namespace {

constexpr uint32_t kFsDirFlags =
    ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE | ZX_FS_RIGHT_ADMIN |
    ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_NOREMOTE;

// TODO: When the dependencies surrounding fs_clone are simplified, this global
// should be removed. fshost and devmgr each supply their own version of
// |fs_clone|, and devmgr-fdio relies on this function being present to
// implement |devmgr_launch|.
const FsManager* g_fshost = nullptr;

zx_status_t MiscDeviceAdded(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE || strcmp(fn, "ramctl") != 0) {
        return ZX_OK;
    }

    zx::vmo ramdisk_vmo = std::move(*static_cast<zx::vmo*>(cookie));
    size_t size;
    zx_status_t status = ramdisk_vmo.get_size(&size);
    if (status != ZX_OK) {
        return ZX_ERR_STOP;
    }

    const char* errmsg;
    zx::vmo vmo;
    status = decompress_bootdata(zx_vmar_root_self(), ramdisk_vmo.get(), 0, size,
                                 vmo.reset_and_get_address(), &errmsg);
    if (status != ZX_OK) {
        printf("fshost: failed to decompress ramdisk: %s\n", errmsg);
        return ZX_ERR_STOP;
    }

    ramdisk_client* client;
    status = ramdisk_create_from_vmo(vmo.release(), &client);
    if (status != ZX_OK) {
        printf("fshost: failed to create ramdisk from BOOTDATA_RAMDISK\n");
    } else {
        printf("fshost: BOOTDATA_RAMDISK attached\n");
    }
    return ZX_ERR_STOP;
}

int RamctlWatcher(void* arg) {
    fbl::unique_fd dirfd(open("/dev/misc", O_DIRECTORY | O_RDONLY));
    if (!dirfd) {
        printf("fshost: failed to open /dev/misc: %s\n", strerror(errno));
        return -1;
    }
    fdio_watch_directory(dirfd.get(), &MiscDeviceAdded, ZX_TIME_INFINITE, arg);
    return 0;
}

// Setup the loader service to be used by all processes spawned by devmgr.
void setup_loader_service(zx::channel devmgr_loader) {
    loader_service_t* svc;
    zx_status_t status = loader_service_create_fs(nullptr, &svc);
    ;
    if (status != ZX_OK) {
        fprintf(stderr, "fshost: failed to create loader service %d\n", status);
    }
    auto defer = fit::defer([svc] { loader_service_release(svc); });
    status = loader_service_attach(svc, devmgr_loader.release());
    if (status != ZX_OK) {
        fprintf(stderr, "fshost: failed to attach to loader service: %d\n", status);
        return;
    }
    zx_handle_t fshost_loader;
    status = loader_service_connect(svc, &fshost_loader);
    if (status != ZX_OK) {
        fprintf(stderr, "fshost: failed to connect to loader service: %d\n", status);
        return;
    }
    zx_handle_close(dl_set_loader_service(fshost_loader));
}

} // namespace

FshostConnections::FshostConnections(zx::channel devfs_root, zx::channel svc_root,
                                     zx::channel fs_root, zx::event event)
    : devfs_root_(std::move(devfs_root)), svc_root_(std::move(svc_root)),
      fs_root_(std::move(fs_root)), event_(std::move(event)) {}

zx_status_t FshostConnections::Open(const char* path, zx::channel* out_connection) const {
    zx::channel connection;
    zx_status_t status = ZX_OK;
    if (!strcmp(path, "svc")) {
        connection.reset(fdio_service_clone(svc_root_.get()));
    } else if (!strcmp(path, "dev")) {
        connection.reset(fdio_service_clone(devfs_root_.get()));
    } else {
        zx::channel server;
        status = zx::channel::create(0, &connection, &server);
        if (status == ZX_OK) {
            status = fdio_open_at(fs_root_.get(), path, kFsDirFlags, server.release());
        }
    }
    *out_connection = std::move(connection);
    return status;
}

zx_status_t FshostConnections::CreateNamespace() {
    fdio_ns_t* ns;
    zx_status_t status;
    if ((status = fdio_ns_get_installed(&ns)) != ZX_OK) {
        printf("fshost: cannot get namespace: %d\n", status);
        return status;
    }

    if ((status = fdio_ns_bind(ns, "/fs", fs_root_.get())) != ZX_OK) {
        printf("fshost: cannot bind /fs to namespace: %d\n", status);
        return status;
    }
    zx::channel system_connection;
    if ((status = Open("system", &system_connection)) != ZX_OK) {
        printf("fshost: cannot open connection to /system: %d\n", status);
        return status;
    }
    if ((status = fdio_ns_bind(ns, "/system", system_connection.release())) != ZX_OK) {
        printf("fshost: cannot bind /system to namespace: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx::channel fs_clone(const char* path) {
    zx::channel connection;
    if (g_fshost->GetConnections().Open(path, &connection) == ZX_OK) {
        return connection;
    } else {
        return zx::channel();
    }
}

} // namespace devmgr

int main(int argc, char** argv) {
    bool netboot = false;
    bool disable_block_watcher = false;

    enum {
        kNetboot,
        kDisableBlockWatcher,
    };
    option options[] = {
        {"netboot", no_argument, nullptr, kNetboot},
        {"disable-block-watcher", no_argument, nullptr, kDisableBlockWatcher},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", options, nullptr)) != -1) {
        switch (opt) {
        case kNetboot:
            netboot = true;
            break;
        case kDisableBlockWatcher:
            disable_block_watcher = true;
            break;
        }
    }

    zx::channel fs_root(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    zx::channel devfs_root;
    {
        zx::channel devfs_root_remote;
        zx_status_t status = zx::channel::create(0, &devfs_root, &devfs_root_remote);
        ZX_ASSERT(status == ZX_OK);

        fdio_ns_t* ns;
        status = fdio_ns_get_installed(&ns);
        ZX_ASSERT(status == ZX_OK);
        status = fdio_ns_connect(ns, "/dev", ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE,
                                 devfs_root_remote.release());
        ZX_ASSERT_MSG(status == ZX_OK, "fshost: failed to connect to /dev: %s\n",
                      zx_status_get_string(status));
    }
    zx::channel svc_root(zx_take_startup_handle(PA_HND(PA_USER0, 1)));
    zx::channel devmgr_loader(zx_take_startup_handle(PA_HND(PA_USER0, 2)));
    zx::event fshost_event(zx_take_startup_handle(PA_HND(PA_USER1, 0)));

    // First, initialize the local filesystem in isolation.
    auto root = fbl::make_unique<devmgr::FsManager>();

    // Initialize connections to external service managers, and begin
    // monitoring the |fshost_event| for a termination event.
    root->InitializeConnections(std::move(fs_root), std::move(devfs_root), std::move(svc_root),
                                std::move(fshost_event));
    devmgr::g_fshost = root.get();

    // If we have a "/system" ramdisk, start higher level services.
    if (root->IsSystemMounted()) {
        root->FuchsiaStart();
    }

    // Setup the devmgr loader service.
    devmgr::setup_loader_service(std::move(devmgr_loader));

    // If there is a ramdisk, setup the ramctl watcher.
    zx::vmo ramdisk_vmo(zx_take_startup_handle(PA_HND(PA_VMO_BOOTDATA, 0)));
    if (ramdisk_vmo.is_valid()) {
        thrd_t t;
        int err = thrd_create_with_name(&t, &devmgr::RamctlWatcher, &ramdisk_vmo, "ramctl-watcher");
        if (err != thrd_success) {
            printf("fshost: failed to start ramctl-watcher: %d\n", err);
        }
        thrd_detach(t);
    }

    if (!disable_block_watcher) {
        block_device_watcher(std::move(root), zx::job::default_job(), netboot);
    } else {
        // Keep the process alive so that the loader service continues to be supplied
        // to the devmgr. Otherwise the devmgr will segfault.
        zx::nanosleep(zx::time::infinite());
    }
    printf("fshost: terminating (block device watcher finished?)\n");
    return 0;
}
