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

#include "block-watcher.h"
#include "fs-manager.h"

namespace devmgr {
namespace {

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

// Initialize the fshost namespace.
//
// |fs_root_client| is mapped to "/fs", and represents the filesystem of devmgr.
zx_status_t BindNamespace(zx::channel fs_root_client) {
    fdio_ns_t* ns;
    zx_status_t status;
    if ((status = fdio_ns_get_installed(&ns)) != ZX_OK) {
        printf("fshost: cannot get namespace: %d\n", status);
        return status;
    }

    // Bind "/fs".
    if ((status = fdio_ns_bind(ns, "/fs", fs_root_client.release())) != ZX_OK) {
        printf("fshost: cannot bind /fs to namespace: %d\n", status);
        return status;
    }

    // Bind "/system".
    {
        zx::channel client, server;
        if ((status = zx::channel::create(0, &client, &server)) != ZX_OK) {
            return status;
        }
        if ((status = fdio_open("/fs/system", ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_ADMIN,
                                server.release())) != ZX_OK) {
            printf("fshost: cannot open connection to /system: %d\n", status);
            return status;
        }
        if ((status = fdio_ns_bind(ns, "/system", client.release())) != ZX_OK) {
            printf("fshost: cannot bind /system to namespace: %d\n", status);
            return status;
        }
    }
    return ZX_OK;
}

} // namespace
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

    zx::channel fs_root_server(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    zx::channel devmgr_loader(zx_take_startup_handle(PA_HND(PA_USER0, 2)));
    zx::channel fshost_export_server(zx_take_startup_handle(PA_HND(PA_USER0, 3)));
    zx::event fshost_event(zx_take_startup_handle(PA_HND(PA_USER1, 0)));

    // First, initialize the local filesystem in isolation.
    fbl::unique_ptr<devmgr::FsManager> fs_manager;
    zx_status_t status = devmgr::FsManager::Create(std::move(fshost_event), &fs_manager);
    if (status != ZX_OK) {
        printf("fshost: Cannot create FsManager\n");
        return status;
    }

    // First, begin serving the "fs_root" on behalf of devmgr.
    status = fs_manager->ServeRoot(std::move(fs_root_server));
    if (status != ZX_OK) {
        printf("fshost: Cannot serve devmgr's root filesystem\n");
        return status;
    }
    status = fs_manager->ServeFshostRoot(std::move(fshost_export_server));
    if (status != ZX_OK) {
        printf("fshost: Cannot serve export directory\n");
        return status;
    }

    // Now that we are serving the fs_root, acquire a new connection
    // to place in our own namespace.
    zx::channel fs_root_client;
    status = zx::channel::create(0, &fs_root_client, &fs_root_server);
    if (status != ZX_OK) {
        return ZX_OK;
    }
    status = fs_manager->ServeRoot(std::move(fs_root_server));
    if (status != ZX_OK) {
        printf("fshost: Cannot serve devmgr's root filesystem\n");
        return status;
    }


    // Initialize namespace, and begin monitoring the |fshost_event| for a termination event.
    status = devmgr::BindNamespace(std::move(fs_root_client));
    if (status != ZX_OK) {
        printf("fshost: cannot bind namespace\n");
        return status;
    }
    fs_manager->WatchExit();

    // Setup the devmgr loader service.
    devmgr::setup_loader_service(std::move(devmgr_loader));

    // If there is a ramdisk, setup the ramctl filesystems.
    zx::vmo ramdisk_vmo(zx_take_startup_handle(PA_HND(PA_VMO_BOOTDATA, 0)));
    if (ramdisk_vmo.is_valid()) {
        thrd_t t;
        int err = thrd_create_with_name(&t, &devmgr::RamctlWatcher, &ramdisk_vmo, "ramctl-filesystems");
        if (err != thrd_success) {
            printf("fshost: failed to start ramctl-filesystems: %d\n", err);
        }
        thrd_detach(t);
    }

    if (!disable_block_watcher) {
        BlockDeviceWatcher(std::move(fs_manager), netboot);
    } else {
        // Keep the process alive so that the loader service continues to be supplied
        // to the devmgr. Otherwise the devmgr will segfault.
        zx::nanosleep(zx::time::infinite());
    }
    printf("fshost: terminating (block device filesystems finished?)\n");
    return 0;
}
