// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <stdio.h>
#include <utility>

#include <launchpad/launchpad.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/util.h>
#include <lib/zx/debuglog.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include "bootfs-loader-service.h"
#include "bootfs-service.h"

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

struct LaunchNextProcessArgs {
    fbl::RefPtr<bootsvc::BootfsService> bootfs;
};

// Launch the next process in the boot chain.
// It will receive:
// - stdout wired up via a debuglog handle
// - The boot cmdline arguments, via envp
// - A namespace containing a /boot, serviced by bootsvc
// - A loader that can load libraries from /boot, serviced by bootsvc
// - A handle to the root job
// - A handle to the root resource (TODO(teisenbe): This should be a channel to
//                                  a service for obtaining the root resource)
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
        launchpad_add_handle(lp, debuglog.release(), PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO | 0));
    }

    launchpad_add_handle(lp, zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)), PA_HND(PA_RESOURCE, 0));

    const char* errmsg;
    if ((status = launchpad_go(lp, nullptr, &errmsg)) < 0) {
        printf("bootsvc: launchpad %s failed: %s: %s\n", next_program, errmsg,
               zx_status_get_string(status));
    } else {
        printf("bootsvc: launched %s\n", next_program);
    }
    return 0;
}

void StartLaunchNextProcessThread(const fbl::RefPtr<bootsvc::BootfsService>& bootfs) {
    auto args = fbl::make_unique<LaunchNextProcessArgs>();
    args->bootfs = bootfs;

    thrd_t t;
    int status = thrd_create(&t, LaunchNextProcess, args.release());
    ZX_ASSERT(status == thrd_success);
    status = thrd_detach(t);
    ZX_ASSERT(status == thrd_success);
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
    zx_status_t status = bootsvc::BootfsService::Create(std::move(bootfs_vmo), loop.dispatcher(),
                                                        &bootfs_svc);
    ZX_ASSERT_MSG(status == ZX_OK, "BootfsService creation failed: %s\n",
                  zx_status_get_string(status));

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
    StartLaunchNextProcessThread(bootfs_svc);

    // Begin serving the bootfs fileystem and loader
    loop.Run();
    return 0;
}
