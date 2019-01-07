// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bootdata/decompress.h>

#include <fbl/function.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/unique_fd.h>
#include <fs-management/ramdisk.h>
#include <launchpad/launchpad.h>
#include <lib/bootfs/parser.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>
#include <loader-service/loader-service.h>

#include <zircon/boot/bootdata.h>
#include <zircon/device/vfs.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <utility>

#include "fshost.h"

namespace devmgr {
namespace {

// When adding VMOs to the boot filesystem, add them under the directory
// /boot/VMO_SUBDIR. This constant must end, but not start, with a slash.
#define VMO_SUBDIR "kernel/"
#define VMO_SUBDIR_LEN (sizeof(VMO_SUBDIR) - 1)

#define LAST_PANIC_FILEPATH "log/last-panic.txt"

constexpr uint32_t kFsDirFlags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_ADMIN |
                                 ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_NOREMOTE;

struct BootdataRamdisk : public fbl::SinglyLinkedListable<fbl::unique_ptr<BootdataRamdisk>> {
public:
    explicit BootdataRamdisk(zx::vmo vmo) : vmo_(std::move(vmo)) {}

    zx::vmo TakeRamdisk() {
        return std::move(vmo_);
    }
private:
    zx::vmo vmo_;
};

using RamdiskList = fbl::SinglyLinkedList<fbl::unique_ptr<BootdataRamdisk>>;

// TODO: When the dependencies surrounding fs_clone are simplified, this global
// should be removed. fshost and devmgr each supply their own version of
// |fs_clone|, and devmgr-fdio relies on this function being present to
// implement |devmgr_launch|.
const FsManager* g_fshost = nullptr;

zx_status_t SetupBootfsVmo(const fbl::unique_ptr<FsManager>& root, uint32_t n, zx_handle_t vmo) {
    uint64_t size;
    zx_status_t status = zx_vmo_get_size(vmo, &size);
    if (status != ZX_OK) {
        printf("devmgr: failed to get bootfs#%u size (%d)\n", n, status);
        return status;
    }
    if (size == 0) {
        return ZX_OK;
    }

    // map the vmo so that ps will account for it
    // NOTE: will leak the mapping in case the bootfs is thrown away later
    uintptr_t address;
    zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, size, &address);

    if (!root->IsSystemMounted()) {
        status = root->MountSystem();
        if (status != ZX_OK) {
            printf("devmgr: failed to mount /system (%d)\n", status);
            return status;
        }
    }
    // We need to duplicate |vmo| because |bootfs::Create| takes ownership of the
    // |vmo| and closes it during |bootfs::Destroy|. However, our |bootfs::Parse|
    // callback will further store |vmo| in memfs.
    zx::vmo bootfs_vmo;
    status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, bootfs_vmo.reset_and_get_address());
    if (status != ZX_OK) {
        printf("devmgr: failed to duplicate vmo for /system (%d)\n", status);
        return status;
    }
    bootfs::Parser bfs;
    if (bfs.Init(zx::unowned_vmo(bootfs_vmo)) == ZX_OK) {
        bfs.Parse([&root, vmo](const bootfs_entry_t *entry) -> zx_status_t {
                      // printf("bootfs: %s @%zd (%zd bytes)\n", path, off, len);
                      root.get()->SystemfsAddFile(entry->name, vmo, entry->data_off,
                                                  entry->data_len);
                      return ZX_OK;
                  });
    }
    root->SystemfsSetReadonly(getenv("zircon.system.writable") == nullptr);
    return ZX_OK;
}

zx_status_t MiscDeviceAdded(int dirfd, int event, const char* fn, void* cookie) {
    auto bootdata_ramdisk_list = static_cast<RamdiskList*>(cookie);
    if (event != WATCH_EVENT_ADD_FILE || strcmp(fn, "ramctl") != 0) {
        return ZX_OK;
    }

    while (!bootdata_ramdisk_list->is_empty()) {
        zx::vmo ramdisk_vmo = bootdata_ramdisk_list->pop_front()->TakeRamdisk();
        uint64_t size;
        zx_status_t status = ramdisk_vmo.get_size(&size);
        if (status != ZX_OK) {
            printf("fshost: cannot get size of ramdisk_vmo: %d\n", status);
            return status;
        }

        struct ramdisk_client* client;
        if (create_ramdisk_from_vmo(ramdisk_vmo.release(), &client) != ZX_OK) {
            printf("fshost: failed to create ramdisk from BOOTDATA_RAMDISK\n");
        } else {
            printf("fshost: BOOTDATA_RAMDISK attached\n");
        }
    }

    return ZX_ERR_STOP;
}

int RamctlWatcher(void* arg) {
    fbl::unique_ptr<RamdiskList> ramdisk_list(static_cast<RamdiskList*>(arg));
    fbl::unique_fd dirfd(open("/dev/misc", O_DIRECTORY | O_RDONLY));
    if (!dirfd) {
        printf("fshost: failed to open /dev/misc: %s\n", strerror(errno));
        return -1;
    }
    fdio_watch_directory(dirfd.get(), &MiscDeviceAdded, ZX_TIME_INFINITE, ramdisk_list.get());
    return 0;
}

#define HND_BOOTFS(n) PA_HND(PA_VMO_BOOTFS, n)
#define HND_BOOTDATA(n) PA_HND(PA_VMO_BOOTDATA, n)

void SetupBootfs(const fbl::unique_ptr<FsManager>& root,
                 const fbl::unique_ptr<RamdiskList>& ramdisk_list) {
    unsigned idx = 0;

    zx::vmo vmo;
    for (unsigned n = 0; vmo.reset(zx_take_startup_handle(HND_BOOTDATA(n))), vmo.is_valid(); n++) {
        bootdata_t bootdata;
        zx_status_t status = vmo.read(&bootdata, 0, sizeof(bootdata));
        if (status < 0) {
            continue;
        }
        if ((bootdata.type != BOOTDATA_CONTAINER) || (bootdata.extra != BOOTDATA_MAGIC)) {
            printf("devmgr: bootdata item does not contain bootdata\n");
            continue;
        }
        if (!(bootdata.flags & BOOTDATA_FLAG_V2)) {
            printf("devmgr: bootdata v1 no longer supported\n");
            continue;
        }

        size_t len = bootdata.length;
        size_t off = sizeof(bootdata);

        while (len > sizeof(bootdata)) {
            zx_status_t status = vmo.read(&bootdata, off, sizeof(bootdata));
            if (status < 0) {
                break;
            }
            size_t itemlen = BOOTDATA_ALIGN(sizeof(bootdata_t) + bootdata.length);
            if (itemlen > len) {
                printf("devmgr: bootdata item too large (%zd > %zd)\n", itemlen, len);
                break;
            }
            switch (bootdata.type) {
            case BOOTDATA_CONTAINER:
                printf("devmgr: unexpected bootdata container header\n");
                continue;
            case BOOTDATA_BOOTFS_DISCARD:
                // this was already unpacked for us by userboot and bootsvc
                break;
            case BOOTDATA_BOOTFS_BOOT:
                // These should have been consumed by userboot and bootsvc.
                printf("devmgr: unexpected boot-type bootfs\n");
                break;
            case BOOTDATA_BOOTFS_SYSTEM: {
                const char* errmsg;
                zx_handle_t bootfs_vmo;
                status = decompress_bootdata(zx_vmar_root_self(), vmo.get(),
                                             off, bootdata.length + sizeof(bootdata_t),
                                             &bootfs_vmo, &errmsg);
                if (status < 0) {
                    printf("devmgr: failed to decompress bootdata: %s\n", errmsg);
                } else {
                    SetupBootfsVmo(root, idx++, bootfs_vmo);
                }
                break;
            }
            case BOOTDATA_RAMDISK: {
                const char* errmsg;
                zx_handle_t ramdisk_vmo;
                status = decompress_bootdata(
                    zx_vmar_root_self(), vmo.get(),
                    off, bootdata.length + sizeof(bootdata_t),
                    &ramdisk_vmo, &errmsg);
                if (status != ZX_OK) {
                    printf("fshost: failed to decompress bootdata: %s\n",
                           errmsg);
                } else {
                    auto ramdisk = fbl::make_unique<BootdataRamdisk>(zx::vmo(ramdisk_vmo));
                    ramdisk_list->push_front(std::move(ramdisk));
                }
                break;
            }
            default:
                break;
            }
            off += itemlen;
            len -= itemlen;
        }

        // Close the VMO once we've finished processing it.
        vmo.reset();
    }
}

// Setup the loader service to be used by all processes spawned by devmgr.
void setup_loader_service(zx::channel devmgr_loader) {
    loader_service_t* svc;
    zx_status_t status = loader_service_create_fs(nullptr, &svc);;
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

zx::channel FshostConnections::Open(const char* path) const {
    if (!strcmp(path, "svc")) {
        return zx::channel(fdio_service_clone(svc_root_.get()));
    }
    if (!strcmp(path, "dev")) {
        return zx::channel(fdio_service_clone(devfs_root_.get()));
    }
    zx::channel client, server;
    if (zx::channel::create(0, &client, &server) != ZX_OK) {
        return zx::channel();
    }
    if (fdio_open_at(fs_root_.get(), path, kFsDirFlags, server.release()) != ZX_OK) {
        return zx::channel();
    }
    return client;
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
    if ((status = fdio_ns_bind(ns, "/system", Open("system").release())) != ZX_OK) {
        printf("devmgr: cannot bind /system to namespace: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx::channel fs_clone(const char* path) {
    return g_fshost->GetConnections().Open(path);
}

} // namespace devmgr

int main(int argc, char** argv) {
    using namespace devmgr;

    printf("fshost: started.\n");

    bool netboot = false;
    while (argc > 1) {
        if (!strcmp(argv[1], "--netboot")) {
            netboot = true;
        } else {
            printf("fshost: unknown option '%s'\n", argv[1]);
        }
        argc--;
        argv++;
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
        status = fdio_ns_connect(ns, "/dev", ZX_FS_RIGHT_READABLE, devfs_root_remote.release());
        ZX_ASSERT_MSG(status == ZX_OK, "fshost: failed to connect to /dev: %s\n",
                      zx_status_get_string(status));
    }
    zx::channel svc_root(zx_take_startup_handle(PA_HND(PA_USER0, 2)));
    zx::channel devmgr_loader(zx_take_startup_handle(PA_HND(PA_USER0, 3)));
    zx::event fshost_event(zx_take_startup_handle(PA_HND(PA_USER1, 0)));

    // First, initialize the local filesystem in isolation.
    fbl::unique_ptr<FsManager> root = fbl::make_unique<FsManager>();

    // Populate the FsManager and RamdiskList with data supplied from
    // startup handles passed to fshost.
    fbl::unique_ptr<RamdiskList> bootdata_ramdisk_list = fbl::make_unique<RamdiskList>();
    SetupBootfs(root, bootdata_ramdisk_list);

    // Initialize connections to external service managers, and begin
    // monitoring the |fshost_event| for a termination event.
    root->InitializeConnections(std::move(fs_root), std::move(devfs_root),
                                std::move(svc_root), std::move(fshost_event));
    g_fshost = root.get();

    // If we have a "/system" ramdisk, start higher level services.
    if (root->IsSystemMounted()) {
        root->FuchsiaStart();
    }

    // Setup the devmgr loader service.
    setup_loader_service(std::move(devmgr_loader));

    if (!bootdata_ramdisk_list->is_empty()) {
        thrd_t th;
        RamdiskList* ramdisk_list = bootdata_ramdisk_list.release();
        int err = thrd_create_with_name(&th, &RamctlWatcher, ramdisk_list, "ramctl-watcher");
        if (err != thrd_success) {
            printf("fshost: failed to start ramctl-watcher: %d\n", err);
            delete ramdisk_list;
        } else {
            thrd_detach(th);
        }
    }

    block_device_watcher(std::move(root), zx::job::default_job(), netboot);

    printf("fshost: terminating (block device watcher finished?)\n");
    return 0;
}
