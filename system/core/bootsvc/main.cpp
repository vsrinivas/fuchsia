// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <stdio.h>
#include <utility>

#include <bootdata/decompress.h>
#include <fbl/vector.h>
#include <launchpad/launchpad.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/util.h>
#include <lib/zx/debuglog.h>
#include <zircon/boot/bootdata.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include "bootfs-loader-service.h"
#include "bootfs-service.h"
#include "util.h"

namespace {

// Wire up stdout so that printf() and friends work.
void SetupStdout() {
    zx::debuglog h;
    if (zx::debuglog::create(zx::resource(), 0, &h) < 0) {
        return;
    }
    fdio_t* logger;
    if ((logger = fdio_logger_create(h.release())) == nullptr) {
        return;
    }
    close(1);
    fdio_bind_to_fd(logger, 1, 0);
}

// Load the cmdline arguments overrides from the bootfs
void LoadCmdlineOverridesFromBootfs(const fbl::RefPtr<bootsvc::BootfsService>& bootfs) {
    // TODO(teisenbe): Rename this file
    const char* config_file = "/config/devmgr";

    zx::vmo vmo;
    uint64_t file_size;
    zx_status_t status = bootfs->Open(config_file, &vmo, &file_size);
    if (status != ZX_OK) {
        return;
    }

    auto cfg = fbl::make_unique<char[]>(file_size + 1);
    ZX_ASSERT(cfg);

    status = vmo.read(cfg.get(), 0, file_size);
    if (status != ZX_OK) {
        printf("zx_vmo_read on /boot/config/devmgr BOOTFS VMO: %d (%s)\n",
               status, zx_status_get_string(status));
        return;
    }
    cfg[file_size] = '\0';

    // putenv() below takes ownership of pieces of this memory, so just release
    // ownership of it now.
    char* x = cfg.release();
    while (*x) {
        // skip any leading whitespace
        while (isspace(*x)) {
            x++;
        }

        // find the next line (seek for CR or NL)
        char* next = x;
        for (;;) {
            // eof? we're all done then
            if (*next == 0) {
                return;
            }
            if ((*next == '\r') || (*next == '\n')) {
                *next++ = 0;
                break;
            }
            next++;
        }

        // process line if not a comment and not a zero-length name
        if ((*x != '#') && (*x != '=')) {
            for (char* y = x; *y != 0; y++) {
                // space in name is invalid, give up
                if (isspace(*y)) {
                    break;
                }
                // valid looking env entry? store it
                if (*y == '=') {
                    putenv(x);
                    break;
                }
            }
        }

        x = next;
    }
}

// Set up the channel we will use for passing the root resource off.  We
// embed the root resource in a channel to make it harder to accidentally
// leave a handle to it in some process on the way to devmgr.
zx::channel CreateResourceChannel() {
    zx::resource resource(zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)));
    ZX_ASSERT_MSG(resource.is_valid(), "bootsvc: did not receive resource handle\n");

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &server, &client);
    ZX_ASSERT(status == ZX_OK);

    zx_handle_t handles[] = { resource.release() };
    status = server.write(0, nullptr, 0, handles, fbl::count_of(handles));
    ZX_ASSERT(status == ZX_OK);
    return client;
}

struct LaunchNextProcessArgs {
    fbl::RefPtr<bootsvc::BootfsService> bootfs;
    fbl::Vector<zx::vmo> bootdata;
};

// Launch the next process in the boot chain.
// It will receive:
// - stdout wired up via a debuglog handle
// - The boot cmdline arguments, via envp
// - A namespace containing a /boot, serviced by bootsvc
// - A loader that can load libraries from /boot, serviced by bootsvc
// - A handle to the root job
// - A handle to each of the bootdata VMOs the kernel provided
// - A handle to a channel containing the root resource, with type kResourceChannelHandleType
int LaunchNextProcess(void* raw_ctx) {
    fbl::unique_ptr<LaunchNextProcessArgs> args(static_cast<LaunchNextProcessArgs*>(raw_ctx));

    const char* next_program = getenv("bootsvc.next");
    if (next_program == nullptr) {
        next_program = "bin/devmgr";
    }

    // Open the executable we will start next
    zx::vmo program;
    uint64_t file_size;
    zx_status_t status = args->bootfs->Open(next_program, &program, &file_size);
    ZX_ASSERT_MSG(status == ZX_OK, "bootsvc: failed to open '%s': %s\n", next_program,
                  zx_status_get_string(status));

    zx::channel resource_client = CreateResourceChannel();

    // Get the bootfs fuchsia.io.Node service channel that we will hand to the
    // next process in the boot chain.
    zx::channel bootfs_conn;
    status = args->bootfs->CreateRootConnection(&bootfs_conn);
    ZX_ASSERT_MSG(status == ZX_OK, "bootfs conn creation failed: %s\n",
                  zx_status_get_string(status));

    const char* nametable[1] = { };
    uint32_t count = 0;

    launchpad_t* lp;
    launchpad_create(0, next_program, &lp);
    launchpad_load_from_vmo(lp, program.release());
    launchpad_clone(lp, LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);

    launchpad_add_handle(lp, bootfs_conn.release(), PA_HND(PA_NS_DIR, count));
    nametable[count++] = "/boot";

    ZX_ASSERT(count <= fbl::count_of(nametable));
    launchpad_set_nametable(lp, count, nametable);

    zx::debuglog debuglog;
    status = zx::debuglog::create(zx::resource(), 0, &debuglog);
    if (status != ZX_OK) {
        launchpad_abort(lp, status, "bootsvc: cannot create debuglog handle");
    } else {
        launchpad_add_handle(lp, debuglog.release(), PA_HND(PA_FDIO_LOGGER,
                                                            FDIO_FLAG_USE_FOR_STDIO | 0));
    }

    launchpad_add_handle(lp, resource_client.release(), bootsvc::kResourceChannelHandleType);

    unsigned bootdata_idx = 0;
    for (zx::vmo& bootdata : args->bootdata) {
        launchpad_add_handle(lp, bootdata.release(), PA_HND(PA_VMO_BOOTDATA, bootdata_idx++));
    }

    const char* errmsg;
    if ((status = launchpad_go(lp, nullptr, &errmsg)) < 0) {
        printf("bootsvc: launchpad %s failed: %s: %s\n", next_program, errmsg,
               zx_status_get_string(status));
    } else {
        printf("bootsvc: launched %s\n", next_program);
    }
    return 0;
}

void StartLaunchNextProcessThread(const fbl::RefPtr<bootsvc::BootfsService>& bootfs,
                                  fbl::Vector<zx::vmo> bootdata) {
    auto args = fbl::make_unique<LaunchNextProcessArgs>();
    args->bootfs = bootfs;
    args->bootdata = std::move(bootdata);

    thrd_t t;
    int status = thrd_create(&t, LaunchNextProcess, args.release());
    ZX_ASSERT(status == thrd_success);
    status = thrd_detach(t);
    ZX_ASSERT(status == thrd_success);
}

// Checks if there are any additions to the BOOT bootfs and if there is a
// crashlog from the bootloader.  Modifies the bootdata_vmos vector as necessary
zx_status_t ProcessBootdata(const fbl::RefPtr<bootsvc::BootfsService>& bootfs,
                            const fbl::Vector<zx::vmo>& bootdata_vmos) {
    for (const zx::vmo& vmo : bootdata_vmos) {
        bootdata_t bootdata;
        zx_status_t status = vmo.read(&bootdata, 0, sizeof(bootdata));
        if (status < 0) {
            continue;
        }
        if ((bootdata.type != BOOTDATA_CONTAINER) || (bootdata.extra != BOOTDATA_MAGIC)) {
            printf("bootsvc: bootdata item does not contain bootdata\n");
            continue;
        }
        if (!(bootdata.flags & BOOTDATA_FLAG_V2)) {
            printf("bootsvc: bootdata v1 no longer supported\n");
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
                printf("bootsvc: bootdata item too large (%zd > %zd)\n", itemlen, len);
                break;
            }
            switch (bootdata.type) {
            case BOOTDATA_CONTAINER:
                printf("bootsvc: unexpected bootdata container header\n");
                break;
            case BOOTDATA_BOOTFS_BOOT: {
                const char* errmsg;
                zx::vmo bootfs_vmo;
                status = decompress_bootdata(zx_vmar_root_self(), vmo.get(),
                                             off, bootdata.length + sizeof(bootdata_t),
                                             bootfs_vmo.reset_and_get_address(), &errmsg);
                if (status != ZX_OK) {
                    printf("bootsvc: failed to decompress bootfs: %s\n", errmsg);
                    break;
                }
                status = bootfs->AddBootfs(std::move(bootfs_vmo));
                if (status != ZX_OK) {
                    printf("bootsvc: failed to add bootfs: %s\n", errmsg);
                    break;
                }

                // Mark that we've already processed this one.
                bootdata.type = BOOTDATA_BOOTFS_DISCARD;
                vmo.write(&bootdata.type, off + offsetof(bootdata_t, type),
                          sizeof(bootdata.type));
                break;
            }
            default:
                break;
            }
            off += itemlen;
            len -= itemlen;
        }
    }
    return ZX_OK;
}

} // namespace

int main(int argc, char** argv) {
    SetupStdout();
    printf("bootsvc: Starting...\n");

    // Close the loader-service channel so the service can go away.
    // We won't use it any more (no dlopen calls in this process).
    zx_handle_close(dl_set_loader_service(ZX_HANDLE_INVALID));

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

    zx::vmo bootfs_vmo(zx_take_startup_handle(PA_HND(PA_VMO_BOOTFS, 0)));
    ZX_ASSERT(bootfs_vmo.is_valid());

    // Set up the bootfs service
    printf("bootsvc: Creating bootfs service...\n");
    fbl::RefPtr<bootsvc::BootfsService> bootfs_svc;
    zx_status_t status = bootsvc::BootfsService::Create(loop.dispatcher(), &bootfs_svc);
    ZX_ASSERT_MSG(status == ZX_OK, "BootfsService creation failed: %s\n",
                  zx_status_get_string(status));
    status = bootfs_svc->AddBootfs(std::move(bootfs_vmo));
    ZX_ASSERT_MSG(status == ZX_OK, "bootfs add failed: %s\n", zx_status_get_string(status));

    // Process the bootdata to get additional bootfs parts
    printf("bootsvc: Processing bootdata...\n");
    fbl::Vector<zx::vmo> bootdata = bootsvc::RetrieveBootdata();
    status = ProcessBootdata(bootfs_svc, bootdata);
    ZX_ASSERT_MSG(status == ZX_OK, "Procesing bootdata failed: %s\n", zx_status_get_string(status));

    // Apply any cmdline overrides from bootfs
    printf("bootsvc: Loading boot cmdline overrides...\n");
    LoadCmdlineOverridesFromBootfs(bootfs_svc);

    // Consume certain VMO types from the startup handle table
    printf("bootsvc: Loading kernel VMOs...\n");
    bootfs_svc->PublishStartupVmos(PA_VMO_VDSO, "PA_VMO_VDSO");
    bootfs_svc->PublishStartupVmos(PA_VMO_KERNEL_FILE, "PA_VMO_KERNEL_FILE");

    // Creating the loader service
    printf("bootsvc: Creating loader service...\n");
    fbl::RefPtr<bootsvc::BootfsLoaderService> loader;
    status = bootsvc::BootfsLoaderService::Create(bootfs_svc, loop.dispatcher(), &loader);
    ZX_ASSERT_MSG(status == ZX_OK, "BootfsLoaderService creation failed: %s\n",
                  zx_status_get_string(status));

    // Switch to the local loader service backed directly by the primary bootfs
    // to allow us to load the next process.
    zx::channel local_loader_conn;
    status = loader->Connect(&local_loader_conn);
    ZX_ASSERT_MSG(status == ZX_OK, "failed to connect to BootfsLoaderService : %s\n",
                  zx_status_get_string(status));
    zx_handle_close(dl_set_loader_service(local_loader_conn.release()));

    // Launch the next process in the chain.  This must be in a thread, since
    // it may issue requests to the loader, which runs in the async loop that
    // starts running after this.
    printf("bootsvc: Launching next process...\n");
    StartLaunchNextProcessThread(bootfs_svc, std::move(bootdata));

    // Begin serving the bootfs fileystem and loader
    loop.Run();
    return 0;
}
