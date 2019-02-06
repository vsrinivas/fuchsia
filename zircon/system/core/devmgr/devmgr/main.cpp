// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>
#include <utility>

#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <launchpad/launchpad.h>
#include <loader-service/loader-service.h>
#include <zircon/boot/bootdata.h>
#include <zircon/device/vfs.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/policy.h>

#include <lib/async-loop/cpp/loop.h>
#include <lib/devmgr-launcher/processargs.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/util.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

#include "../shared/env.h"
#include "../shared/fdio.h"
#include "../shared/log.h"
#include "coordinator.h"
#include "devhost-loader-service.h"
#include "devmgr.h"

namespace {

struct {
    // The handle used to transmit messages to appmgr.
    zx::channel appmgr_client;

    // The handle used by appmgr to serve incoming requests.
    // If appmgr cannot be launched within a timeout, this handle is closed.
    zx::channel appmgr_server;

    zx::resource root_resource;
    zx::unowned_job root_job;
    zx::job svc_job;
    zx::job fuchsia_job;
    zx::channel svchost_outgoing;

    zx::channel fs_root;
} g_handles;

// Wait for the requested file.  Its parent directory must exist.
zx_status_t wait_for_file(const char* path, zx::time deadline) {
    char path_copy[PATH_MAX];
    if (strlen(path) >= PATH_MAX) {
        return ZX_ERR_INVALID_ARGS;
    }
    strcpy(path_copy, path);

    char* last_slash = strrchr(path_copy, '/');
    // Waiting on the root of the fs or paths with no slashes is not supported by this function
    if (last_slash == path_copy || last_slash == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    last_slash[0] = 0;
    char* dirname = path_copy;
    char* basename = last_slash + 1;

    auto watch_func = [](int dirfd, int event, const char* fn, void* cookie) -> zx_status_t {
        auto basename = static_cast<const char*>(cookie);
        if (event != WATCH_EVENT_ADD_FILE) {
            return ZX_OK;
        }
        if (!strcmp(fn, basename)) {
            return ZX_ERR_STOP;
        }
        return ZX_OK;
    };

    fbl::unique_fd dirfd(open(dirname, O_RDONLY));
    if (!dirfd.is_valid()) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status = fdio_watch_directory(dirfd.get(), watch_func, deadline.get(),
                                              reinterpret_cast<void*>(basename));
    if (status == ZX_ERR_STOP) {
        return ZX_OK;
    }
    return status;
}

zx_status_t devmgr_launch_load(void* ctx, launchpad_t* lp, const char* file) {
    return launchpad_load_from_file(lp, file);
}

void do_autorun(const char* name, const char* env) {
    const char* cmd = getenv(env);
    if (cmd != nullptr) {
        devmgr::devmgr_launch_cmdline(env, g_handles.svc_job, name, &devmgr_launch_load, nullptr,
                                      cmd, nullptr, nullptr, 0, nullptr, FS_ALL);
    }
}

// Get the root resource from the startup handle.  Not receiving the startup
// handle is logged, but not fatal.  In test environments, it would not be
// present.
zx_status_t fetch_root_resource(zx::resource* root_resource) {
    // Read the root resource out of its channel
    zx::channel root_resource_channel(
        zx_take_startup_handle(DEVMGR_LAUNCHER_ROOT_RESOURCE_CHANNEL_HND));
    if (!root_resource_channel) {
        fprintf(stderr, "devmgr: did not receive root resource channel, assuming test "
                        "environment and continuing\n");
        return ZX_OK;
    }
    uint32_t actual_handles = 0;
    zx_status_t status = root_resource_channel.read(
        0, nullptr, 0, nullptr, root_resource->reset_and_get_address(), 1, &actual_handles);
    if (status != ZX_OK) {
        return status;
    }
    return actual_handles == 1 ? ZX_OK : ZX_ERR_UNAVAILABLE;
}

int fuchsia_starter(void* arg) {
    auto coordinator = static_cast<devmgr::Coordinator*>(arg);
    bool appmgr_started = false;
    bool autorun_started = false;
    bool drivers_loaded = false;

    size_t appmgr_timeout = 20;
    zx::time deadline = zx::deadline_after(zx::sec(appmgr_timeout));

    do {
        zx_status_t status =
            coordinator->fshost_event().wait_one(FSHOST_SIGNAL_READY, deadline, nullptr);
        if (status == ZX_ERR_TIMED_OUT) {
            if (g_handles.appmgr_server.is_valid()) {
                if (coordinator->require_system()) {
                    fprintf(stderr, "devmgr: appmgr not launched in %zus, closing appmgr handle\n",
                            appmgr_timeout);
                }
                g_handles.appmgr_server.reset();
            }
            deadline = zx::time::infinite();
            continue;
        }
        if (status != ZX_OK) {
            fprintf(stderr, "devmgr: error waiting on fuchsia start event: %d\n", status);
            break;
        }
        status = coordinator->fshost_event().signal(FSHOST_SIGNAL_READY, 0);
        if (status != ZX_OK) {
            fprintf(stderr, "devmgr: error signaling fshost: %d\n", status);
        }

        if (!drivers_loaded) {
            // we're starting the appmgr because /system is present
            // so we also signal the device coordinator that those
            // drivers are now loadable
            coordinator->set_system_available(true);
            coordinator->ScanSystemDrivers();
            drivers_loaded = true;
        }

        const char* argv_appmgr[] = {"/system/bin/appmgr"};
        struct stat s;
        if (!appmgr_started && stat(argv_appmgr[0], &s) == 0) {
            unsigned int appmgr_hnd_count = 0;
            zx_handle_t appmgr_hnds[2] = {};
            uint32_t appmgr_ids[2] = {};
            if (g_handles.appmgr_server.is_valid()) {
                assert(appmgr_hnd_count < fbl::count_of(appmgr_hnds));
                appmgr_hnds[appmgr_hnd_count] = g_handles.appmgr_server.release();
                appmgr_ids[appmgr_hnd_count] = PA_DIRECTORY_REQUEST;
                appmgr_hnd_count++;
            }
            devmgr::devmgr_launch(g_handles.fuchsia_job, "appmgr", &devmgr_launch_load, nullptr,
                                  fbl::count_of(argv_appmgr), argv_appmgr, nullptr, -1, appmgr_hnds,
                                  appmgr_ids, appmgr_hnd_count, nullptr, FS_FOR_APPMGR);
            appmgr_started = true;
        }
        if (!autorun_started) {
            do_autorun("autorun:system", "zircon.autorun.system");
            autorun_started = true;
        }
    } while (!appmgr_started);
    return 0;
}

int console_starter(void* arg) {
    // if no kernel shell on serial uart, start a sh there
    printf("devmgr: shell startup\n");

    // If we got a TERM environment variable (aka a TERM=... argument on
    // the kernel command line), pass this down; otherwise pass TERM=uart.
    const char* term = getenv("TERM");
    if (term == nullptr) {
        term = "TERM=uart";
    } else {
        term -= sizeof("TERM=") - 1;
    }

    const char* device = getenv("console.path");
    if (!device) {
        device = "/dev/misc/console";
    }

    const char* envp[] = {
        term,
        nullptr,
    };

    zx_status_t status = wait_for_file(device, zx::time::infinite());
    if (status != ZX_OK) {
        printf("devmgr: failed to wait for console '%s'\n", device);
        return 1;
    }
    fbl::unique_fd fd(open(device, O_RDWR));
    if (!fd.is_valid()) {
        printf("devmgr: failed to open console '%s'\n", device);
        return 1;
    }

    const char* argv_sh[] = {"/boot/bin/sh"};
    devmgr::devmgr_launch(g_handles.svc_job, "sh:console", &devmgr_launch_load, nullptr,
                          fbl::count_of(argv_sh), argv_sh, envp, fd.release(), nullptr, nullptr, 0,
                          nullptr, FS_ALL);
    return 0;
}

int pwrbtn_monitor_starter(void* arg) {
    const char* name = "pwrbtn-monitor";
    const char* argv[] = {"/boot/bin/pwrbtn-monitor"};
    int argc = 1;

    zx::job job_copy;
    zx_status_t status =
        g_handles.svc_job.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE, &job_copy);
    if (status != ZX_OK) {
        printf("svc_job.duplicate failed %s\n", zx_status_get_string(status));
        return 1;
    }

    launchpad_t* lp;
    launchpad_create(job_copy.get(), name, &lp);

    status = launchpad_load_from_file(lp, argv[0]);
    if (status != ZX_OK) {
        launchpad_abort(lp, status, "cannot load file");
    }
    launchpad_set_args(lp, argc, argv);

    // create a namespace containing /dev/class/input and /dev/misc
    const char* nametable[2] = {};
    uint32_t count = 0;
    zx::channel fs_handle = devmgr::fs_clone("dev/class/input");
    if (fs_handle.is_valid()) {
        nametable[count] = "/input";
        launchpad_add_handle(lp, fs_handle.release(), PA_HND(PA_NS_DIR, count++));
    } else {
        launchpad_abort(lp, ZX_ERR_BAD_STATE, "devmgr: failed to clone /dev/class/input");
    }

    // Ideally we'd only expose /dev/misc/dmctl, but we do not support exposing
    // single files
    fs_handle = devmgr::fs_clone("dev/misc");
    if (fs_handle.is_valid()) {
        nametable[count] = "/misc";
        launchpad_add_handle(lp, fs_handle.release(), PA_HND(PA_NS_DIR, count++));
    } else {
        launchpad_abort(lp, ZX_ERR_BAD_STATE, "devmgr: failed to clone /dev/misc");
    }
    launchpad_set_nametable(lp, count, nametable);

    zx::debuglog debuglog;
    if ((status = zx::debuglog::create(zx::resource(), 0, &debuglog) < 0)) {
        launchpad_abort(lp, status, "devmgr: cannot create debuglog handle");
    } else {
        launchpad_add_handle(lp, debuglog.release(),
                             PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO | 0));
    }

    const char* errmsg;
    if ((status = launchpad_go(lp, nullptr, &errmsg)) < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n", argv[0], name, errmsg, status);
    } else {
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
    return 0;
}

void start_console_shell() {
    // start a shell on the kernel console if it isn't already running a shell
    if (!devmgr::getenv_bool("kernel.shell", false)) {
        thrd_t t;
        if ((thrd_create_with_name(&t, console_starter, nullptr, "console-starter")) ==
            thrd_success) {
            thrd_detach(t);
        }
    }
}

zx_status_t fuchsia_create_job() {
    zx_status_t status = zx::job::create(*g_handles.root_job, 0u, &g_handles.fuchsia_job);
    if (status != ZX_OK) {
        printf("devmgr: unable to create fuchsia job: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }

    g_handles.fuchsia_job.set_property(ZX_PROP_NAME, "fuchsia", 7);

    const zx_policy_basic_t fuchsia_job_policy[] = {
        {.condition = ZX_POL_NEW_PROCESS, .policy = ZX_POL_ACTION_DENY}};

    status =
        g_handles.fuchsia_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC, fuchsia_job_policy,
                                         fbl::count_of(fuchsia_job_policy));
    if (status != ZX_OK) {
        printf("devmgr: unable to set policy fuchsia job: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }

    return ZX_OK;
}

zx_status_t svchost_start(bool require_system) {
    printf("devmgr: svc init\n");

    zx::channel dir_request;
    zx::debuglog logger;
    zx::channel appmgr_svc_req;
    zx::channel appmgr_svc;

    zx_status_t status = zx::channel::create(0, &dir_request, &g_handles.svchost_outgoing);
    if (status != ZX_OK) {
        return status;
    }

    status = zx::debuglog::create(zx::resource(), 0, &logger);
    if (status != ZX_OK) {
        return status;
    }

    status = zx::channel::create(0, &appmgr_svc_req, &appmgr_svc);
    if (status != ZX_OK) {
        return status;
    }

    status =
        fdio_service_connect_at(g_handles.appmgr_client.get(), "svc", appmgr_svc_req.release());
    if (status != ZX_OK) {
        return status;
    }

    const char* name = "svchost";
    const char* argv[2] = {
        "/boot/bin/svchost",
        require_system ? "--require-system" : nullptr,
    };
    int argc = require_system ? 2 : 1;

    zx::job svc_job_copy;
    status = g_handles.svc_job.duplicate(
        ZX_RIGHTS_BASIC | ZX_RIGHT_MANAGE_JOB | ZX_RIGHT_MANAGE_PROCESS, &svc_job_copy);
    if (status != ZX_OK) {
        return status;
    }

    zx::job root_job_copy;
    status = g_handles.root_job->duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHTS_PROPERTY |
                                               ZX_RIGHT_ENUMERATE | ZX_RIGHT_MANAGE_PROCESS,
                                           &root_job_copy);
    if (status != ZX_OK) {
        return status;
    }

    launchpad_t* lp = nullptr;
    launchpad_create(svc_job_copy.get(), name, &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    launchpad_add_handle(lp, dir_request.release(), PA_DIRECTORY_REQUEST);
    launchpad_add_handle(lp, logger.release(), PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO));

    // Remove once svchost hosts the tracelink service itself.
    launchpad_add_handle(lp, appmgr_svc.release(), PA_HND(PA_USER0, 0));

    // Give svchost a restricted root job handle. svchost is already a privileged system service
    // as it controls system-wide process launching. With the root job it can consolidate a few
    // services such as crashsvc and the profile service.
    launchpad_add_handle(lp, root_job_copy.release(), PA_HND(PA_USER0, 1));

    // Give svchost access to /dev/class/sysmem, to enable svchost to forward sysmem service
    // requests to the sysmem driver.  Create a namespace containing /dev/class/sysmem.
    const char* nametable[1] = {};
    uint32_t count = 0;
    zx::channel fs_handle = devmgr::fs_clone("dev/class/sysmem");
    if (fs_handle.is_valid()) {
        nametable[count] = "/sysmem";
        launchpad_add_handle(lp, fs_handle.release(), PA_HND(PA_NS_DIR, count++));
    } else {
        launchpad_abort(lp, ZX_ERR_BAD_STATE, "devmgr: failed to clone /dev/class/sysmem");
        // The launchpad_go() call below will fail, but will still free lp.
    }

    launchpad_set_nametable(lp, count, nametable);

    const char* errmsg = nullptr;
    if ((status = launchpad_go(lp, nullptr, &errmsg)) < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n", argv[0], name, errmsg, status);
    } else {
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
    return ZX_OK;
}

void fshost_start(devmgr::Coordinator* coordinator) {
    // assemble handles to pass down to fshost
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t types[fbl::count_of(handles)];
    size_t n = 0;
    zx_handle_t ldsvc;

    // pass / and /svc handles to fsboot
    if (zx_channel_create(0, g_handles.fs_root.reset_and_get_address(), &handles[0]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER0, 0);
    }
    if ((handles[n] = devmgr::fs_clone("svc").release()) != ZX_HANDLE_INVALID) {
        types[n++] = PA_HND(PA_USER0, 2);
    }
    if (zx_channel_create(0, &ldsvc, &handles[n]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER0, 3);
    } else {
        ldsvc = ZX_HANDLE_INVALID;
    }

    // pass fuchsia start event to fshost
    zx::event fshost_event_duplicate;
    if (coordinator->fshost_event().duplicate(ZX_RIGHT_SAME_RIGHTS, &fshost_event_duplicate) ==
        ZX_OK) {
        handles[n] = fshost_event_duplicate.release();
        types[n++] = PA_HND(PA_USER1, 0);
    }

    // pass bootdata VMOs to fshost
    for (uint32_t m = 0; n < fbl::count_of(handles); m++) {
        uint32_t type = PA_HND(PA_VMO_BOOTDATA, m);
        if ((handles[n] = zx_take_startup_handle(type)) == ZX_HANDLE_INVALID) {
            break;
        }
        zx_status_t status = coordinator->SetBootdata(zx::unowned_vmo(handles[n]));
        if (status != ZX_OK) {
            fprintf(stderr, "devmgr: failed to set bootdata: %d\n", status);
            break;
        }
        types[n++] = type;
    }

    // pass VDSO VMOS to fshost
    for (uint32_t m = 0; n < fbl::count_of(handles); m++) {
        uint32_t type = PA_HND(PA_VMO_VDSO, m);
        if (m == 0) {
            // By this point, launchpad has already moved PA_HND(PA_VMO_VDSO, 0) into a static.
            handles[n] = ZX_HANDLE_INVALID;
            launchpad_get_vdso_vmo(&handles[n]);
        } else {
            handles[n] = zx_take_startup_handle(type);
        }

        if (handles[n] != ZX_HANDLE_INVALID) {
            types[n++] = type;
        } else {
            break;
        }
    }

    // pass KERNEL FILE VMOS to fsboot
    for (uint32_t m = 0; n < fbl::count_of(handles); m++) {
        uint32_t type = PA_HND(PA_VMO_KERNEL_FILE, m);
        if ((handles[n] = zx_take_startup_handle(type)) != ZX_HANDLE_INVALID) {
            types[n++] = type;
        } else {
            break;
        }
    }

    const char* argv[] = {"/boot/bin/fshost", "--netboot"};
    int argc = (devmgr::getenv_bool("netsvc.netboot", false) ||
                devmgr::getenv_bool("zircon.system.disable-automount", false))
                   ? 2
                   : 1;

    // Pass zircon.system.* options to the fshost as environment variables
    const char* envp[16];
    unsigned envc = 0;
    char** e = environ;
    while (*e && (envc < fbl::count_of(envp))) {
        if (!strncmp(*e, "zircon.system", strlen("zircon.system"))) {
            envp[envc++] = *e;
        }
        e++;
    }
    envp[envc] = nullptr;

    devmgr::devmgr_launch(g_handles.svc_job, "fshost", &devmgr_launch_load, nullptr, argc, argv,
                          envp, -1, handles, types, n, nullptr, FS_BOOT | FS_DEV);

    // switch to system loader service provided by fshost
    zx_handle_close(dl_set_loader_service(ldsvc));
}

zx::channel bootfs_root_clone() {
    zx::channel boot, boot_remote;
    zx_status_t status = zx::channel::create(0, &boot, &boot_remote);
    if (status != ZX_OK) {
        return zx::channel();
    }

    fdio_ns_t* ns;
    status = fdio_ns_get_installed(&ns);
    ZX_ASSERT(status == ZX_OK);
    status = fdio_ns_connect(ns, "/boot", ZX_FS_RIGHT_READABLE, boot_remote.release());
    if (status != ZX_OK) {
        return zx::channel();
    }
    return boot;
}

void devmgr_vfs_init(devmgr::Coordinator* coordinator) {
    printf("devmgr: vfs init\n");

    fdio_ns_t* ns;
    zx_status_t r;
    r = fdio_ns_get_installed(&ns);
    ZX_ASSERT_MSG(r == ZX_OK, "devmgr: cannot get namespace: %s\n", zx_status_get_string(r));
    r = fdio_ns_bind(ns, "/dev", devmgr::fs_clone("dev").release());
    ZX_ASSERT_MSG(r == ZX_OK, "devmgr: cannot bind /dev to namespace: %s\n",
                  zx_status_get_string(r));

    // Start fshost before binding /system, since it publishes it.
    fshost_start(coordinator);

    if ((r = fdio_ns_bind(ns, "/system", devmgr::fs_clone("system").release())) != ZX_OK) {
        printf("devmgr: cannot bind /system to namespace: %d\n", r);
    }
}

int service_starter(void* arg) {
    // Features like Intel Processor Trace need a dump of ld.so activity.
    // The output has a specific format, and will eventually be recorded
    // via a specific mechanism (magenta tracing support), so we use a specific
    // env var (and don't, for example, piggyback on LD_DEBUG).
    // We enable this pretty early so that we get a trace of as many processes
    // as possible.
    if (getenv(LDSO_TRACE_CMDLINE)) {
        // This takes care of places that clone our environment.
        putenv(strdup(LDSO_TRACE_ENV));
        // There is still devmgr_launch() which does not clone our enviroment.
        // It has its own check.
    }

    char vcmd[64];
    bool netboot = false;
    bool vruncmd = false;
    if (!devmgr::getenv_bool("netsvc.disable", false)) {
        const char* args[] = {"/boot/bin/netsvc", nullptr, nullptr, nullptr, nullptr, nullptr};
        int argc = 1;

        if (devmgr::getenv_bool("netsvc.netboot", false)) {
            args[argc++] = "--netboot";
            netboot = true;
            vruncmd = true;
        }

        if (devmgr::getenv_bool("netsvc.advertise", true)) {
            args[argc++] = "--advertise";
        }

        const char* interface;
        if ((interface = getenv("netsvc.interface")) != nullptr) {
            args[argc++] = "--interface";
            args[argc++] = interface;
        }

        const char* nodename = getenv("zircon.nodename");
        if (nodename) {
            args[argc++] = nodename;
        }

        zx::process proc;
        if (devmgr::devmgr_launch(g_handles.svc_job, "netsvc", &devmgr_launch_load, nullptr, argc,
                                  args, nullptr, -1, nullptr, nullptr, 0, &proc, FS_ALL) == ZX_OK) {
            if (vruncmd) {
                zx_info_handle_basic_t info = {};
                proc.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
                proc.reset();
                snprintf(vcmd, sizeof(vcmd), "dlog -f -t -p %zu", info.koid);
            }
        } else {
            vruncmd = false;
        }
        __UNUSED auto leaked_handle = proc.release();
    }

    auto coordinator = static_cast<devmgr::Coordinator*>(arg);
    if (!devmgr::getenv_bool("virtcon.disable", false)) {
        // pass virtcon.* options along
        const char* envp[16];
        unsigned envc = 0;
        char** e = environ;
        while (*e && (envc < fbl::count_of(envp))) {
            if (!strncmp(*e, "virtcon.", 8)) {
                envp[envc++] = *e;
            }
            e++;
        }
        envp[envc] = nullptr;

        const char* num_shells = coordinator->require_system() && !netboot ? "0" : "3";
        size_t handle_count = 0;
        zx_handle_t handles[2];
        uint32_t types[2];

        zx::channel virtcon_client, virtcon_server;
        zx_status_t status = zx::channel::create(0, &virtcon_client, &virtcon_server);
        if (status == ZX_OK) {
            coordinator->set_virtcon_channel(std::move(virtcon_client));
            handles[handle_count] = virtcon_server.release();
            types[handle_count] = PA_HND(PA_USER0, 0);
            ++handle_count;
        }

        zx::debuglog debuglog;
        status =
            zx::debuglog::create(coordinator->root_resource(), ZX_LOG_FLAG_READABLE, &debuglog);
        if (status == ZX_OK) {
            handles[handle_count] = debuglog.release();
            types[handle_count] = PA_HND(PA_USER0, 1);
            ++handle_count;
        }

        const char* args[] = {"/boot/bin/virtual-console", "--shells", num_shells, "--run", vcmd};
        devmgr::devmgr_launch(g_handles.svc_job, "virtual-console", &devmgr_launch_load, nullptr,
                              vruncmd ? 5 : 3, args, envp, -1, handles, types, handle_count,
                              nullptr, FS_ALL);
    }

    const char* epoch = getenv("devmgr.epoch");
    if (epoch) {
        zx_time_t offset = ZX_SEC(atoi(epoch));
        zx_clock_adjust(coordinator->root_resource().get(), ZX_CLOCK_UTC, offset);
    }

    do_autorun("autorun:boot", "zircon.autorun.boot");

    thrd_t t;
    if ((thrd_create_with_name(&t, fuchsia_starter, coordinator, "fuchsia-starter")) ==
        thrd_success) {
        thrd_detach(t);
    }

    return 0;
}

void ParseArgs(int argc, char** argv, devmgr::DevmgrArgs* out) {
    enum {
        kDriverSearchPath,
        kLoadDriver,
        kSysDeviceDriver,
        kNoLaunchSvchost,
    };
    option options[] = {
        {"driver-search-path", required_argument, nullptr, kDriverSearchPath},
        {"load-driver", required_argument, nullptr, kLoadDriver},
        {"sys-device-driver", required_argument, nullptr, kSysDeviceDriver},
        {"no-launch-svchost", no_argument, nullptr, kNoLaunchSvchost},
    };

    auto print_usage_and_exit = [options]() {
        printf("devmgr: supported arguments:\n");
        for (const auto& option : options) {
            printf("  --%s\n", option.name);
        }
        exit(1);
    };

    auto check_not_duplicated = [print_usage_and_exit](const char* arg) {
        if (arg != nullptr) {
            printf("devmgr: duplicated argument\n");
            print_usage_and_exit();
        }
    };

    // Reset the args state
    *out = devmgr::DevmgrArgs();

    int opt;
    while ((opt = getopt_long(argc, argv, "", options, nullptr)) != -1) {
        switch (opt) {
        case kDriverSearchPath:
            out->driver_search_paths.push_back(optarg);
            break;
        case kLoadDriver:
            out->load_drivers.push_back(optarg);
            break;
        case kSysDeviceDriver:
            check_not_duplicated(out->sys_device_driver);
            out->sys_device_driver = optarg;
            break;
        case kNoLaunchSvchost:
            out->launch_svchost = false;
            break;
        default:
            print_usage_and_exit();
        }
    }
}

zx_status_t CreateDevhostJob(const zx::job& root_job, zx::job* devhost_job_out) {
    printf("devmgr: coordinator_init()\n");

    zx::job devhost_job;
    zx_status_t status = zx::job::create(root_job, 0u, &devhost_job);
    if (status != ZX_OK) {
        log(ERROR, "devcoord: unable to create devhost job\n");
        return status;
    }
    static const zx_policy_basic_t policy[] = {
        {ZX_POL_BAD_HANDLE, ZX_POL_ACTION_EXCEPTION},
    };
    status = devhost_job.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC, &policy,
                                    fbl::count_of(policy));
    if (status != ZX_OK) {
        log(ERROR, "devcoord: zx_job_set_policy() failed\n");
        return status;
    }
    status = devhost_job.set_property(ZX_PROP_NAME, "zircon-drivers", 15);
    if (status != ZX_OK) {
        log(ERROR, "devcoord: zx_job_set_property() failed\n");
        return status;
    }

    *devhost_job_out = std::move(devhost_job);
    return ZX_OK;
}

} // namespace

namespace devmgr {

zx::channel fs_clone(const char* path) {
    if (!strcmp(path, "dev")) {
        return devfs_root_clone();
    }
    if (!strcmp(path, "boot")) {
        return bootfs_root_clone();
    }
    zx::channel h0, h1;
    if (zx::channel::create(0, &h0, &h1) != ZX_OK) {
        return zx::channel();
    }
    zx::unowned_channel fs(g_handles.fs_root);
    int flags = FS_DIR_FLAGS;
    if (!strcmp(path, "hub")) {
        fs = zx::unowned_channel(g_handles.appmgr_client);
    } else if (!strcmp(path, "svc")) {
        flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE;
        fs = zx::unowned_channel(g_handles.svchost_outgoing);
        path = "public";
    } else if (!strncmp(path, "dev/", 4)) {
        fs = devfs_root_borrow();
        path += 4;
    }
    zx_status_t status = fdio_open_at(fs->get(), path, flags, h1.release());
    if (status != ZX_OK) {
        return zx::channel();
    }
    return h0;
}

} // namespace devmgr

int main(int argc, char** argv) {
    printf("devmgr: main()\n");
    for (char** e = environ; *e != nullptr; e++) {
        printf("cmdline: %s\n", *e);
    }
    if (devmgr::getenv_bool("devmgr.verbose", false)) {
        devmgr::log_flags |= LOG_ALL;
    }

    devmgr::DevmgrArgs args;
    ParseArgs(argc, argv, &args);
    // Set up the default values for our arguments if they weren't given.
    if (args.driver_search_paths.size() == 0) {
        args.driver_search_paths.push_back("/boot/driver");
    }
    if (args.sys_device_driver == nullptr) {
        // x86 platforms use acpi as the system device
        // all other platforms use the platform bus
#if defined(__x86_64__)
        args.sys_device_driver = "/boot/driver/bus-acpi.so";
#else
        args.sys_device_driver = "/boot/driver/platform-bus.so";
#endif
    }

    g_handles.root_job = zx::job::default_job();
    g_handles.root_job->set_property(ZX_PROP_NAME, "root", 4);
    bool require_system = devmgr::getenv_bool("devmgr.require-system", false);

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    devmgr::CoordinatorConfig config;
    config.dispatcher = loop.dispatcher();
    config.require_system = require_system;
    config.asan_drivers = devmgr::getenv_bool("devmgr.devhost.asan", false);
    config.suspend_fallback = devmgr::getenv_bool("devmgr.suspend-timeout-fallback", false);
    config.suspend_debug = devmgr::getenv_bool("devmgr.suspend-timeout-debug", false);
    zx_status_t status = fetch_root_resource(&config.root_resource);
    if (status != ZX_OK) {
        fprintf(stderr, "devmgr: did not receive root resource: %d\n", status);
        return 1;
    }
    // TODO: limit to enumerate rights
    status = g_handles.root_job->duplicate(ZX_RIGHT_SAME_RIGHTS, &config.sysinfo_job);
    if (status != ZX_OK) {
        fprintf(stderr, "devmgr: failed to duplicate root job for sysinfo: %d\n", status);
    }
    status = CreateDevhostJob(*g_handles.root_job, &config.devhost_job);
    if (status != ZX_OK) {
        fprintf(stderr, "devmgr: failed to create devhost job: %d\n", status);
        return 1;
    }
    status = zx::event::create(0, &config.fshost_event);
    if (status != ZX_OK) {
        fprintf(stderr, "devmgr: failed to create fshost event: %d\n", status);
        return 1;
    }

    devmgr::Coordinator coordinator(std::move(config));
    status = coordinator.InitializeCoreDevices(args.sys_device_driver);
    if (status != ZX_OK) {
        log(ERROR, "devmgr: failed to initialize core devices\n");
        return 1;
    }

    devmgr::devfs_init(&coordinator.root_device(), loop.dispatcher());
    devfs_publish(&coordinator.root_device(), &coordinator.misc_device());
    devfs_publish(&coordinator.root_device(), &coordinator.sys_device());
    devfs_publish(&coordinator.root_device(), &coordinator.test_device());

    // Check if whatever launched devmgr gave a channel to be connected to /dev.
    // This is for use in tests to let the test environment see devfs.
    zx::channel devfs_client(zx_take_startup_handle(DEVMGR_LAUNCHER_DEVFS_ROOT_HND));
    if (devfs_client.is_valid()) {
        fdio_service_clone_to(devmgr::devfs_root_borrow()->get(), devfs_client.release());
    }

    status = zx::job::create(*g_handles.root_job, 0u, &g_handles.svc_job);
    if (status != ZX_OK) {
        fprintf(stderr, "devmgr: failed to create service job: %d\n", status);
        return 1;
    }
    g_handles.svc_job.set_property(ZX_PROP_NAME, "zircon-services", 16);

    status = fuchsia_create_job();
    if (status != ZX_OK) {
        return 1;
    }

    zx::channel::create(0, &g_handles.appmgr_client, &g_handles.appmgr_server);

    if (args.launch_svchost) {
        status = svchost_start(require_system);
        if (status != ZX_OK) {
            fprintf(stderr, "devmgr: failed to start svchost: %d", status);
            return 1;
        }
    }

    devmgr_vfs_init(&coordinator);

    // If this is not a full Fuchsia build, do not setup appmgr services, as
    // this will delay startup.
    if (!require_system) {
        devmgr::devmgr_disable_appmgr_services();
    }

    thrd_t t;
    if ((thrd_create_with_name(&t, pwrbtn_monitor_starter, nullptr, "pwrbtn-monitor-starter")) ==
        thrd_success) {
        thrd_detach(t);
    }

    start_console_shell();

    if ((thrd_create_with_name(&t, service_starter, &coordinator, "service-starter")) ==
        thrd_success) {
        thrd_detach(t);
    }

    fbl::unique_ptr<devmgr::DevhostLoaderService> loader_service;
    if (devmgr::getenv_bool("devmgr.devhost.strict-linking", false)) {
        status = devmgr::DevhostLoaderService::Create(loop.dispatcher(), &loader_service);
        if (status != ZX_OK) {
            return 1;
        }
        coordinator.set_loader_service(loader_service.get());
    }

    for (const char* path : args.driver_search_paths) {
        devmgr::find_loadable_drivers(
            path, fit::bind_member(&coordinator, &devmgr::Coordinator::DriverAddedInit));
    }
    for (const char* driver : args.load_drivers) {
        devmgr::load_driver(driver,
                            fit::bind_member(&coordinator, &devmgr::Coordinator::DriverAddedInit));
    }

    // Special case early handling for the ramdisk boot
    // path where /system is present before the coordinator
    // starts.  This avoids breaking the "priority hack" and
    // can be removed once the real driver priority system
    // exists.
    if (coordinator.system_available()) {
        coordinator.ScanSystemDrivers();
    }

    if (coordinator.require_system() && !coordinator.system_loaded()) {
        printf(
            "devcoord: full system required, ignoring fallback drivers until /system is loaded\n");
    } else {
        coordinator.UseFallbackDrivers();
    }

    coordinator.PrepareProxy(&coordinator.sys_device());
    coordinator.PrepareProxy(&coordinator.test_device());
    // Initial bind attempt for drivers enumerated at startup.
    coordinator.BindDrivers();

    coordinator.set_running(true);
    status = loop.Run();
    fprintf(stderr, "devmgr: coordinator exited unexpectedly: %d\n", status);
    return status == ZX_OK ? 0 : 1;
}
