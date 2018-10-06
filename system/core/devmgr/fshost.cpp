// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "fshost.h"

#include <bootdata/decompress.h>

#include <fbl/function.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/unique_fd.h>
#include <fs-management/ramdisk.h>
#include <launchpad/launchpad.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
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

#include "bootfs.h"
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
    explicit BootdataRamdisk(zx::vmo vmo) : vmo_(fbl::move(vmo)) {}

    zx::vmo TakeRamdisk() {
        return fbl::move(vmo_);
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

using AddFileFn = fbl::Function<zx_status_t(const char* path, zx_handle_t vmo,
                                            zx_off_t off, size_t len)>;

struct callback_data {
    zx_handle_t vmo;
    unsigned int file_count;
    AddFileFn add_file;
};

zx_status_t callback(void* arg, const bootfs_entry_t* entry) {
    auto cd = static_cast<callback_data*>(arg);
    //printf("bootfs: %s @%zd (%zd bytes)\n", path, off, len);
    cd->add_file(entry->name, cd->vmo, entry->data_off, entry->data_len);
    ++cd->file_count;
    return ZX_OK;
}

zx_status_t SetupBootfsVmo(const fbl::unique_ptr<FsManager>& root, uint32_t n,
                           uint32_t type, zx_handle_t vmo) {
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

    struct callback_data cd = {
        .vmo = vmo,
        .file_count = 0u,
        .add_file = (type == BOOTDATA_BOOTFS_SYSTEM) ?
                fbl::BindMember(root.get(), &FsManager::SystemfsAddFile) :
                fbl::BindMember(root.get(), &FsManager::BootfsAddFile),
    };
    if ((type == BOOTDATA_BOOTFS_SYSTEM) && !root->IsSystemMounted()) {
        status = root->MountSystem();
        if (status != ZX_OK) {
            printf("devmgr: failed to mount /system (%d)\n", status);
            return status;
        }
    }
    // We need to duplicate |vmo| because |bootfs_create| takes ownership of the
    // |vmo| and closes it during |bootfs_destroy|. However, we've stored |vmo|
    // in |cd|, and |callback| will further store |vmo| in memfs.
    zx::vmo bootfs_vmo;
    status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, bootfs_vmo.reset_and_get_address());
    if (status != ZX_OK) {
        printf("devmgr: failed to duplicate vmo for /system (%d)\n", status);
        return status;
    }
    Bootfs bfs;
    if (Bootfs::Create(fbl::move(bootfs_vmo), &bfs) == ZX_OK) {
        bfs.Parse(callback, &cd);
        bfs.Destroy();
    }
    if (type == BOOTDATA_BOOTFS_SYSTEM) {
        root->SystemfsSetReadonly(getenv("zircon.system.writable") == nullptr);
    }
    return ZX_OK;
}

void SetupLastCrashlog(const fbl::unique_ptr<FsManager>& root, zx_handle_t vmo_in,
                       uint64_t off_in, size_t sz) {
    printf("devmgr: last crashlog is %zu bytes\n", sz);
    zx_handle_t vmo;
    if (copy_vmo(vmo_in, off_in, sz, &vmo) != ZX_OK) {
        return;
    }
    root->BootfsAddFile(LAST_PANIC_FILEPATH, vmo, 0, sz);
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

        char path[PATH_MAX + 1];
        if (create_ramdisk_from_vmo(ramdisk_vmo.release(), path) != ZX_OK) {
            printf("fshost: failed to create ramdisk from BOOTDATA_RAMDISK\n");
        } else {
            printf("fshost: BOOTDATA_RAMDISK attached as %s\n", path);
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

    zx::vmo vmo(zx_take_startup_handle(HND_BOOTFS(0)));
    if (vmo.is_valid()) {
        SetupBootfsVmo(root, idx++, BOOTDATA_BOOTFS_BOOT, vmo.release());
    } else {
        printf("devmgr: missing primary bootfs?!\n");
    }

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
                // this was already unpacked for us by userboot
                break;
            case BOOTDATA_BOOTFS_BOOT:
            case BOOTDATA_BOOTFS_SYSTEM: {
                const char* errmsg;
                zx_handle_t bootfs_vmo;
                status = decompress_bootdata(zx_vmar_root_self(), vmo.get(),
                                             off, bootdata.length + sizeof(bootdata_t),
                                             &bootfs_vmo, &errmsg);
                if (status < 0) {
                    printf("devmgr: failed to decompress bootdata: %s\n", errmsg);
                } else {
                    SetupBootfsVmo(root, idx++, bootdata.type, bootfs_vmo);
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
                    ramdisk_list->push_front(fbl::move(ramdisk));
                }
                break;
            }
            case BOOTDATA_LAST_CRASHLOG:
                SetupLastCrashlog(root, vmo.get(), off + sizeof(bootdata_t), bootdata.length);
                break;
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

// Look for VMOs passed as startup handles of PA_HND_TYPE type, and add them to
// the filesystem under the path /boot/VMO_SUBDIR_LEN/<vmo-name>.
void FetchVmos(const fbl::unique_ptr<FsManager>& root, uint_fast8_t type,
               const char* debug_type_name) {
    for (uint_fast16_t i = 0; true; ++i) {
        zx_handle_t vmo = zx_take_startup_handle(PA_HND(type, i));
        if (vmo == ZX_HANDLE_INVALID)
            break;

        if (type == PA_VMO_VDSO && i == 0) {
            // The first vDSO is the default vDSO.  Since we've stolen
            // the startup handle, launchpad won't find it on its own.
            // So point launchpad at it.
            launchpad_set_vdso_vmo(vmo);
        }

        // The vDSO VMOs have names like "vdso/default", so those
        // become VMO files at "/boot/kernel/vdso/default".
        char name[VMO_SUBDIR_LEN + ZX_MAX_NAME_LEN] = VMO_SUBDIR;
        size_t size;
        zx_status_t status = zx_object_get_property(vmo, ZX_PROP_NAME,
                                                    name + VMO_SUBDIR_LEN,
                                                    sizeof(name) - VMO_SUBDIR_LEN);
        if (status != ZX_OK) {
            printf("devmgr: zx_object_get_property on %s %u: %s\n",
                   debug_type_name, i, zx_status_get_string(status));
            continue;
        }
        status = zx_vmo_get_size(vmo, &size);
        if (status != ZX_OK) {
            printf("devmgr: zx_vmo_get_size on %s %u: %s\n",
                   debug_type_name, i, zx_status_get_string(status));
            continue;
        }
        if (size == 0) {
            // empty vmos do not get installed
            zx_handle_close(vmo);
            continue;
        }
        if (!strcmp(name + VMO_SUBDIR_LEN, "crashlog")) {
            // the crashlog has a special home
            strcpy(name, LAST_PANIC_FILEPATH);
        }
        status = root->BootfsAddFile(name, vmo, 0, size);
        if (status != ZX_OK) {
            printf("devmgr: failed to add %s %u to filesystem: %s\n",
                   debug_type_name, i, zx_status_get_string(status));
        }
    }
}

} // namespace

FshostConnections::FshostConnections(zx::channel devfs_root, zx::channel svc_root,
                                     zx::channel fs_root, zx::event event)
    : devfs_root_(fbl::move(devfs_root)), svc_root_(fbl::move(svc_root)),
      fs_root_(fbl::move(fs_root)), event_(fbl::move(event)) {}

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
    if ((status = fdio_ns_create(&ns)) != ZX_OK) {
        printf("fshost: cannot create namespace: %d\n", status);
        return status;
    }

    if ((status = fdio_ns_bind(ns, "/fs", fs_root_.get())) != ZX_OK) {
        printf("fshost: cannot bind /fs to namespace: %d\n", status);
        return status;
    }
    if ((status = fdio_ns_bind(ns, "/dev", Open("dev").release())) != ZX_OK) {
        printf("fshost: cannot bind /dev to namespace: %d\n", status);
        return status;
    }
    if ((status = fdio_ns_bind(ns, "/boot", Open("boot").release())) != ZX_OK) {
        printf("devmgr: cannot bind /boot to namespace: %d\n", status);
        return status;
    }
    if ((status = fdio_ns_bind(ns, "/system", Open("system").release())) != ZX_OK) {
        printf("devmgr: cannot bind /system to namespace: %d\n", status);
        return status;
    }
    if ((status = fdio_ns_install(ns)) != ZX_OK) {
        printf("fshost: cannot install namespace: %d\n", status);
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

    zx::channel _fs_root = zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
    zx::channel devfs_root = zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, 1)));
    zx::channel svc_root = zx::channel(zx_take_startup_handle(PA_HND(PA_USER0, 2)));
    zx_handle_t devmgr_loader = zx_take_startup_handle(PA_HND(PA_USER0, 3));
    zx::event fshost_event = zx::event(zx_take_startup_handle(PA_HND(PA_USER1, 0)));

    // First, initialize the local filesystem in isolation.
    fbl::unique_ptr<FsManager> root = fbl::make_unique<FsManager>();

    // Populate the FsManager and RamdiskList with data supplied from
    // startup handles passed to fshost.
    fbl::unique_ptr<RamdiskList> bootdata_ramdisk_list = fbl::make_unique<RamdiskList>();
    SetupBootfs(root, bootdata_ramdisk_list);
    FetchVmos(root, PA_VMO_VDSO, "PA_VMO_VDSO");
    FetchVmos(root, PA_VMO_KERNEL_FILE, "PA_VMO_KERNEL_FILE");

    // Initialize connections to external service managers, and begin
    // monitoring the |fshost_event| for a termination event.
    root->InitializeConnections(fbl::move(_fs_root), fbl::move(devfs_root),
                                fbl::move(svc_root), fbl::move(fshost_event));
    g_fshost = root.get();

    // If we have a "/system" ramdisk, start higher level services.
    if (root->IsSystemMounted()) {
        root->FuchsiaStart();
    }

    {
        loader_service_t* loader_service;
        zx_status_t status = loader_service_create_fs(nullptr, &loader_service);
        if (status != ZX_OK) {
            printf("fshost: failed to create loader service: %d\n", status);
        } else {
            loader_service_attach(loader_service, devmgr_loader);
            zx_handle_t svc;
            if ((status = loader_service_connect(loader_service, &svc)) != ZX_OK) {
                printf("fshost: failed to connect to loader service: %d\n", status);
            } else {
                // switch from bootfs-loader to system-loader
                zx_handle_close(dl_set_loader_service(svc));
            }
        }
    }

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

    block_device_watcher(fbl::move(root), zx::job::default_job(), netboot);

    printf("fshost: terminating (block device watcher finished?)\n");
    return 0;
}
