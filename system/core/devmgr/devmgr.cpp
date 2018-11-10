// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

#include <fuchsia/crash/c/fidl.h>
#include <launchpad/launchpad.h>
#include <loader-service/loader-service.h>
#include <zircon/boot/bootdata.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/policy.h>

#include <lib/fdio/namespace.h>
#include <lib/fdio/util.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>

#include "bootfs.h"
#include "devhost.h"
#include "devmgr.h"
#include "fshost.h"

namespace devmgr {

// Global flag tracking if devmgr believes this is a full Fuchsia build
// (requiring /system, etc) or not.
bool require_system;

// The handle used to transmit messages to appmgr.
static zx::channel appmgr_req_cli;

// The handle used by appmgr to serve incoming requests.
// If appmgr cannot be launched within a timeout, this handle is closed.
static zx::channel appmgr_req_srv;

static zx_handle_t root_resource_handle;
static zx::unowned_job root_job_handle;
static zx::job svcs_job_handle;
static zx::job fuchsia_job_handle;
static zx::channel exception_channel;
static zx::channel svchost_outgoing;

zx_handle_t virtcon_open;

zx_handle_t get_root_resource() {
    return root_resource_handle;
}

zx::job get_sysinfo_job_root() {
    zx::job h;
    //TODO: limit to enumerate rights
    if (root_job_handle->duplicate(ZX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return zx::job();
    } else {
        return h;
    }
}

static const char* argv_sh[] = {"/boot/bin/sh"};
static const char* argv_appmgr[] = {"/system/bin/appmgr"};

void do_autorun(const char* name, const char* env) {
    const char* cmd = getenv(env);
    if (cmd != nullptr) {
        devmgr_launch_cmdline(env, svcs_job_handle, name,
                              &devmgr_launch_load, nullptr, cmd,
                              nullptr, nullptr, 0, nullptr, FS_ALL);
    }
}

static zx::event fshost_event;

static int fuchsia_starter(void* arg) {
    bool appmgr_started = false;
    bool autorun_started = false;
    bool drivers_loaded = false;

    size_t appmgr_timeout = 10;
    if (getenv_bool("zircon.system.filesystem-check", false)) {
        // This command line option can slow the booting process, so increase
        // the timeout here to compensate.
        appmgr_timeout *= 2;
    }

    zx::time deadline = zx::deadline_after(zx::sec(appmgr_timeout));

    do {
        zx_status_t status = fshost_event.wait_one(FSHOST_SIGNAL_READY, deadline, nullptr);
        if (status == ZX_ERR_TIMED_OUT) {
            if (appmgr_req_srv.is_valid()) {
                if (require_system) {
                    printf("devmgr: appmgr not launched in 10s, closing appmgr handle\n");
                }
                appmgr_req_srv.reset();
            }
            deadline = zx::time::infinite();
            continue;
        }
        if (status != ZX_OK) {
            printf("devmgr: error waiting on fuchsia start event: %d\n", status);
            break;
        }
        fshost_event.signal(FSHOST_SIGNAL_READY, 0);

        if (!drivers_loaded) {
            // we're starting the appmgr because /system is present
            // so we also signal the device coordinator that those
            // drivers are now loadable
            load_system_drivers();
            drivers_loaded = true;
        }

        struct stat s;
        if (!appmgr_started && stat(argv_appmgr[0], &s) == 0) {
            unsigned int appmgr_hnd_count = 0;
            zx_handle_t appmgr_hnds[2] = {};
            uint32_t appmgr_ids[2] = {};
            if (appmgr_req_srv.is_valid()) {
                assert(appmgr_hnd_count < fbl::count_of(appmgr_hnds));
                appmgr_hnds[appmgr_hnd_count] = appmgr_req_srv.release();
                appmgr_ids[appmgr_hnd_count] = PA_DIRECTORY_REQUEST;
                appmgr_hnd_count++;
            }
            devmgr_launch(fuchsia_job_handle, "appmgr",
                          &devmgr_launch_load, nullptr,
                          fbl::count_of(argv_appmgr), argv_appmgr, nullptr, -1,
                          appmgr_hnds, appmgr_ids, appmgr_hnd_count,
                          nullptr, FS_FOR_APPMGR);
            appmgr_started = true;
        }
        if (!autorun_started) {
            do_autorun("autorun:system", "zircon.autorun.system");
            autorun_started = true;
        }
    } while (!appmgr_started);
    return 0;
}

// Reads messages from crashsvc and launches analyzers for exceptions.
int crash_analyzer_listener(void* arg) {
    for (;;) {
        zx_signals_t observed;
        zx_status_t status =
            exception_channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                       zx::time::infinite(), &observed);
        if (status != ZX_OK) {
            printf("devmgr: crash_analyzer_listener zx_object_wait_one failed: %d\n", status);
            return 1;
        }
        if ((observed & ZX_CHANNEL_READABLE) == 0) {
            printf("devmgr: crash_analyzer_listener: peer closed\n");
            return 1;
        }

        uint32_t exception_type;
        zx_handle_t handles[3];
        uint32_t actual_bytes, actual_handles;
        status =
            exception_channel.read(0, &exception_type, sizeof(exception_type), &actual_bytes,
                                   handles, fbl::count_of(handles), &actual_handles);
        if (status != ZX_OK) {
            printf("devmgr: zx_channel_read failed: %d\n", status);
            continue;
        }
        if (actual_bytes != sizeof(exception_type) || actual_handles != fbl::count_of(handles)) {
            printf("devmgr: zx_channel_read unexpected read size: %d\n", status);
            zx_handle_close_many(handles, actual_handles);
            continue;
        }

        // launchpad always takes ownership of handles (even on failure). It's
        // necessary to resume the thread on failure otherwise the process will
        // hang indefinitely, so copy the thread handle before launch.
        zx_handle_t thread_handle;
        status = zx_handle_duplicate(handles[1], ZX_RIGHT_SAME_RIGHTS, &thread_handle);
        if (status != ZX_OK) {
            printf("devmgr: crash_analyzer_listener: thread handle duplicate failed: %d\n", status);
            // If thread handle duplication failed, try to resume and bail.
            zx_task_resume_from_exception(handles[1], handles[2], ZX_RESUME_TRY_NEXT);
            zx_handle_close_many(handles, fbl::count_of(handles));
            continue;
        }

        printf("devmgr: crash_analyzer_listener: analyzing exception type 0x%x\n", exception_type);

        zx_handle_t appmgr_svc_request = ZX_HANDLE_INVALID;
        zx_handle_t appmgr_svc = ZX_HANDLE_INVALID;

        zx_handle_t analyzer_request = ZX_HANDLE_INVALID;
        zx_handle_t analyzer = ZX_HANDLE_INVALID;
        status = zx_channel_create(0, &analyzer_request, &analyzer);
        if (status != ZX_OK)
            goto cleanup;

        const char* analyzer_command;
        analyzer_command = getenv("crashsvc.analyzer");
        if (analyzer_command && strcmp(analyzer_command, "from-appmgr") == 0) {
            // TODO(abarth|scottmg): Appmgr appears to fail at lookups
            // containing /, so do lookup in two steps ("svc", then "Analyzer")
            // for now. ZX-2265.
            status = zx_channel_create(0, &appmgr_svc_request, &appmgr_svc);
            if (status != ZX_OK)
                goto cleanup;
            status = fdio_service_connect_at(appmgr_req_cli.get(), "svc", appmgr_svc_request);
            if (status != ZX_OK)
                goto cleanup;
            appmgr_svc_request = ZX_HANDLE_INVALID;
            status = fdio_service_connect_at(appmgr_svc, fuchsia_crash_Analyzer_Name, analyzer_request);
        } else {
            status = fdio_service_connect_at(svchost_outgoing.get(), "public/" fuchsia_crash_Analyzer_Name, analyzer_request);
        }
        analyzer_request = ZX_HANDLE_INVALID;
        if (status != ZX_OK)
            goto cleanup;
        status = fuchsia_crash_AnalyzerHandleException(analyzer, handles[0], handles[1], handles[2]);
        // fuchsia_crash_AnalyzerHandleException always consumes the handles.
        memset(handles, 0, sizeof(handles));

    cleanup:
        if (analyzer)
            zx_handle_close(analyzer);
        if (appmgr_svc)
            zx_handle_close(appmgr_svc);
        if (handles[0])
            zx_handle_close(handles[0]);
        if (handles[1])
            zx_handle_close(handles[1]);
        if (handles[2])
            zx_handle_close(handles[2]);
        if (status != ZX_OK) {
            printf("devmgr: crash_analyzer_listener: failed to analyze crash: %d (%s)\n",
                   status, zx_status_get_string(status));
            status = zx_task_resume(thread_handle, ZX_RESUME_EXCEPTION | ZX_RESUME_TRY_NEXT);
            if (status != ZX_OK) {
                printf("devmgr: crash_analyzer_listener: zx_task_resume: %d (%s)\n",
                       status, zx_status_get_string(status));
            }
        }
        zx_handle_close(thread_handle);
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

    // Start crashsvc. Bind the exception port now, to avoid missing any crashes
    // that might occur early on before crashsvc has finished initializing.
    // crashsvc writes messages to the passed channel when an analyzer for an
    // exception is required.
    zx::port exception_port;
    zx::channel exception_channel_passed;
    if (zx::port::create(0, &exception_port) == ZX_OK &&
        zx::channel::create(0, &exception_channel, &exception_channel_passed) == ZX_OK &&
        root_job_handle->bind_exception_port(exception_port, 0, 0) == ZX_OK) {
        thrd_t t;
        if ((thrd_create_with_name(&t, crash_analyzer_listener, nullptr,
                                   "crash-analyzer-listener")) == thrd_success) {
            thrd_detach(t);
        }
        zx::job duplicate_job;
        root_job_handle->duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_job);
        zx_handle_t handles[] = {
            duplicate_job.release(),
            exception_port.release(),
            exception_channel_passed.release()
        };
        uint32_t handle_types[] = {PA_HND(PA_USER0, 0), PA_HND(PA_USER0, 1), PA_HND(PA_USER0, 2)};
        static const char* argv_crashsvc[] = {"/boot/bin/crashsvc"};
        devmgr_launch(svcs_job_handle, "crashsvc",
                      &devmgr_launch_load, nullptr,
                      fbl::count_of(argv_crashsvc), argv_crashsvc, nullptr, -1,
                      handles, handle_types, fbl::count_of(handles), nullptr, 0);
    }

    char vcmd[64];
    __UNUSED bool netboot = false;
    bool vruncmd = false;
    if (!getenv_bool("netsvc.disable", false)) {
        const char* args[] = {"/boot/bin/netsvc", nullptr, nullptr, nullptr, nullptr, nullptr};
        int argc = 1;

        if (getenv_bool("netsvc.netboot", false)) {
            args[argc++] = "--netboot";
            netboot = true;
            vruncmd = true;
        }

        if (getenv_bool("netsvc.advertise", true)) {
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
        if (devmgr_launch(svcs_job_handle, "netsvc",
                          &devmgr_launch_load, nullptr, argc, args,
                          nullptr, -1, nullptr, nullptr, 0, &proc, FS_ALL) == ZX_OK) {
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

    if (!getenv_bool("virtcon.disable", false)) {
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

        const char* num_shells = require_system && !netboot ? "0" : "3";

        uint32_t type = PA_HND(PA_USER0, 0);
        zx_handle_t h = ZX_HANDLE_INVALID;
        zx_channel_create(0, &h, &virtcon_open);
        const char* args[] = {"/boot/bin/virtual-console", "--shells", num_shells, "--run", vcmd};
        devmgr_launch(svcs_job_handle, "virtual-console",
                      &devmgr_launch_load, nullptr,
                      vruncmd ? 5 : 3, args, envp, -1,
                      &h, &type, (h == ZX_HANDLE_INVALID) ? 0 : 1, nullptr, FS_ALL);
    }

    const char* epoch = getenv("devmgr.epoch");
    if (epoch) {
        zx_time_t offset = ZX_SEC(atoi(epoch));
        zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
    }

    do_autorun("autorun:boot", "zircon.autorun.boot");

    thrd_t t;
    if ((thrd_create_with_name(&t, fuchsia_starter, nullptr, "fuchsia-starter")) == thrd_success) {
        thrd_detach(t);
    }

    return 0;
}

static int console_starter(void* arg) {
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
    if (!device)
        device = "/dev/misc/console";

    const char* envp[] = {
        term,
        nullptr,
    };
    for (unsigned n = 0; n < 30; n++) {
        int fd;
        if ((fd = open(device, O_RDWR)) >= 0) {
            devmgr_launch(svcs_job_handle, "sh:console",
                          &devmgr_launch_load, nullptr,
                          fbl::count_of(argv_sh), argv_sh, envp, fd, nullptr, nullptr, 0, nullptr, FS_ALL);
            break;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    }
    return 0;
}

static int pwrbtn_monitor_starter(void* arg) {
    const char* name = "pwrbtn-monitor";
    const char* argv[] = {"/boot/bin/pwrbtn-monitor"};
    int argc = 1;

    zx::job job_copy;
    svcs_job_handle.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_WRITE, &job_copy);

    launchpad_t* lp;
    launchpad_create(job_copy.get(), name, &lp);

    zx_status_t status = devmgr_launch_load(nullptr, lp, argv[0]);
    if (status != ZX_OK) {
        launchpad_abort(lp, status, "cannot load file");
    }
    launchpad_set_args(lp, argc, argv);

    // create a namespace containing /dev/class/input and /dev/misc
    const char* nametable[2] = {};
    uint32_t count = 0;
    zx::channel fs_handle = fs_clone("dev/class/input");
    if (fs_handle.is_valid()) {
        nametable[count] = "/input";
        launchpad_add_handle(lp, fs_handle.release(), PA_HND(PA_NS_DIR, count++));
    } else {
        launchpad_abort(lp, ZX_ERR_BAD_STATE, "devmgr: failed to clone /dev/class/input");
    }

    // Ideally we'd only expose /dev/misc/dmctl, but we do not support exposing
    // single files
    fs_handle = fs_clone("dev/misc");
    if (fs_handle.is_valid()) {
        nametable[count] = "/misc";
        launchpad_add_handle(lp, fs_handle.release(), PA_HND(PA_NS_DIR, count++));
    } else {
        launchpad_abort(lp, ZX_ERR_BAD_STATE, "devmgr: failed to clone /dev/misc");
    }
    launchpad_set_nametable(lp, count, nametable);

    zx_handle_t debuglog;
    if ((status = zx_debuglog_create(ZX_HANDLE_INVALID, 0, &debuglog) < 0)) {
        launchpad_abort(lp, status, "devmgr: cannot create debuglog handle");
    } else {
        launchpad_add_handle(lp, debuglog, PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO | 0));
    }

    const char* errmsg;
    if ((status = launchpad_go(lp, nullptr, &errmsg)) < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n",
               argv[0], name, errmsg, status);
    } else {
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
    return 0;
}

static void start_console_shell() {
    // start a shell on the kernel console if it isn't already running a shell
    if (!getenv_bool("kernel.shell", false)) {
        thrd_t t;
        if ((thrd_create_with_name(&t, console_starter, nullptr, "console-starter")) == thrd_success) {
            thrd_detach(t);
        }
    }
}

static void load_cmdline_from_bootfs() {
    uint32_t file_size;
    zx_handle_t vmo = devmgr_load_file("/boot/config/devmgr", &file_size);
    if (vmo == ZX_HANDLE_INVALID) {
        return;
    }

    auto cfg = fbl::make_unique<char[]>(file_size + 1);
    if (cfg == nullptr) {
        zx_handle_close(vmo);
        return;
    }

    zx_status_t status = zx_vmo_read(vmo, cfg.get(), 0, file_size);
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

static zx_status_t fuchsia_create_job() {
    zx_status_t status = zx::job::create(*root_job_handle, 0u, &fuchsia_job_handle);
    if (status != ZX_OK) {
        printf("devmgr: unable to create fuchsia job: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }

    fuchsia_job_handle.set_property(ZX_PROP_NAME, "fuchsia", 7);

    const zx_policy_basic_t fuchsia_job_policy[] = {
        {.condition = ZX_POL_NEW_PROCESS, .policy = ZX_POL_ACTION_DENY}};

    status = fuchsia_job_handle.set_policy(ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC,
                                           fuchsia_job_policy, fbl::count_of(fuchsia_job_policy));
    if (status != ZX_OK) {
        printf("devmgr: unable to set policy fuchsia job: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }

    return ZX_OK;
}

} // namespace devmgr

int main(int argc, char** argv) {
    using namespace devmgr;

    // Close the loader-service channel so the service can go away.
    // We won't use it any more (no dlopen calls in this process).
    zx_handle_close(dl_set_loader_service(ZX_HANDLE_INVALID));

    devmgr_io_init();

    root_resource_handle = zx_take_startup_handle(PA_HND(PA_RESOURCE, 0));
    root_job_handle = zx::job::default_job();

    printf("devmgr: main()\n");

    devfs_init(*root_job_handle);

    root_job_handle->set_property(ZX_PROP_NAME, "root", 4);

    zx_status_t status = zx::job::create(*root_job_handle, 0u, &svcs_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    svcs_job_handle.set_property(ZX_PROP_NAME, "zircon-services", 16);

    if (fuchsia_create_job() != ZX_OK)
        return 1;

    zx::channel::create(0, &appmgr_req_cli, &appmgr_req_srv);
    zx::event::create(0, &fshost_event);

    bootfs_create_from_startup_handle();

    load_cmdline_from_bootfs();
    char** e = environ;
    while (*e) {
        printf("cmdline: %s\n", *e++);
    }

    require_system = getenv_bool("devmgr.require-system", false);

    devmgr_svc_init();
    devmgr_vfs_init();

    // if we're not a full fuchsia build, no point to set up appmgr services
    // which will just cause things attempting to access it to block until
    // we give up on the appmgr 10s later
    if (!require_system) {
        devmgr_disable_appmgr_services();
    }

    thrd_t t;
    if ((thrd_create_with_name(&t, pwrbtn_monitor_starter, nullptr,
                               "pwrbtn-monitor-starter")) == thrd_success) {
        thrd_detach(t);
    }

    start_console_shell();

    if ((thrd_create_with_name(&t, service_starter, nullptr, "service-starter")) ==
        thrd_success) {
        thrd_detach(t);
    }

    coordinator();
    printf("devmgr: coordinator exited?!\n");
    return 0;
}

namespace devmgr {

static zx_handle_t fs_root;

static Bootfs bootfs;

static zx_status_t load_object(void* ctx, const char* name, zx_handle_t* vmo_out) {
    char tmp[256];
    if (snprintf(tmp, sizeof(tmp), "lib/%s", name) >= (int)sizeof(tmp)) {
        return ZX_ERR_BAD_PATH;
    }
    auto bootfs = static_cast<Bootfs*>(ctx);
    zx::vmo vmo;
    zx_status_t status = bootfs->Open(tmp, &vmo, nullptr);
    if (status == ZX_OK) {
        *vmo_out = vmo.release();
    }
    return status;
}

static zx_status_t load_abspath(void* ctx, const char* name, zx_handle_t* vmo) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t publish_data_sink(void* ctx, const char* name, zx_handle_t vmo) {
    zx_handle_close(vmo);
    return ZX_ERR_NOT_SUPPORTED;
}

static const loader_service_ops_t loader_ops = {
    .load_object = load_object,
    .load_abspath = load_abspath,
    .publish_data_sink = publish_data_sink,
    .finalizer = nullptr,
};

static loader_service_t* loader_service;

#define MAXHND ZX_CHANNEL_MAX_MSG_HANDLES

void bootfs_create_from_startup_handle() {
    zx::vmo bootfs_vmo(zx_take_startup_handle(PA_HND(PA_VMO_BOOTFS, 0)));
    if ((!bootfs_vmo.is_valid()) ||
        (Bootfs::Create(fbl::move(bootfs_vmo), &bootfs) != ZX_OK)) {
        printf("devmgr: cannot find and open bootfs\n");
        exit(1);
    }

    // create a local loader service backed directly by the primary bootfs
    // to allow us to load the fshost (since we don't have filesystems before
    // the fshost starts up).
    zx_handle_t ldsvc;
    if ((loader_service_create(nullptr, &loader_ops, &bootfs, &loader_service) != ZX_OK) ||
        (loader_service_connect(loader_service, &ldsvc) != ZX_OK)) {
        printf("devmgr: cannot create loader service\n");
        exit(1);
    }

    // set the bootfs-loader as the default loader service for now
    zx_handle_close(dl_set_loader_service(ldsvc));
}

void fshost_start() {
    // assemble handles to pass down to fshost
    zx_handle_t handles[MAXHND];
    uint32_t types[MAXHND];
    size_t n = 0;
    zx_handle_t ldsvc;

    // pass /, /dev, and /svc handles to fsboot
    if (zx_channel_create(0, &fs_root, &handles[0]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER0, 0);
    }
    if ((handles[n] = devfs_root_clone().release()) != ZX_HANDLE_INVALID) {
        types[n++] = PA_HND(PA_USER0, 1);
    }
    if ((handles[n] = fs_clone("svc").release()) != ZX_HANDLE_INVALID) {
        types[n++] = PA_HND(PA_USER0, 2);
    }
    if (zx_channel_create(0, &ldsvc, &handles[n]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER0, 3);
    } else {
        ldsvc = ZX_HANDLE_INVALID;
    }

    // pass primary bootfs to fshost
    handles[n] = bootfs.DuplicateVmo().release();
    if (handles[n] != ZX_HANDLE_INVALID) {
        types[n++] = PA_HND(PA_VMO_BOOTFS, 0);
    }

    // pass fuchsia start event to fshost
    zx::event fshost_event_duplicate;
    if (fshost_event.duplicate(ZX_RIGHT_SAME_RIGHTS, &fshost_event_duplicate) == ZX_OK) {
        handles[n] = fshost_event_duplicate.release();
        types[n++] = PA_HND(PA_USER1, 0);
    }

    // pass bootdata VMOs to fshost
    for (uint32_t m = 0; n < MAXHND; m++) {
        uint32_t type = PA_HND(PA_VMO_BOOTDATA, m);
        if ((handles[n] = zx_take_startup_handle(type)) != ZX_HANDLE_INVALID) {
            devmgr_set_bootdata(zx::unowned_vmo(handles[n]));
            types[n++] = type;
        } else {
            break;
        }
    }

    // pass VDSO VMOS to fshost
    for (uint32_t m = 0; n < MAXHND; m++) {
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
    for (uint32_t m = 0; n < MAXHND; m++) {
        uint32_t type = PA_HND(PA_VMO_KERNEL_FILE, m);
        if ((handles[n] = zx_take_startup_handle(type)) != ZX_HANDLE_INVALID) {
            types[n++] = type;
        } else {
            break;
        }
    }

    const char* argv[] = {"/boot/bin/fshost", "--netboot"};
    int argc = (getenv_bool("netsvc.netboot", false) ||
                getenv_bool("zircon.system.disable-automount", false))
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

    devmgr_launch(svcs_job_handle, "fshost",
                  &devmgr_launch_load, nullptr, argc, argv, envp, -1,
                  handles, types, n, nullptr, 0);

    // switch to system loader service provided by fshost
    zx_handle_close(dl_set_loader_service(ldsvc));
}

zx_handle_t devmgr_load_file(const char* path, uint32_t* out_size) {
    if (strncmp(path, "/boot/", 6)) {
        return ZX_HANDLE_INVALID;
    }
    zx::vmo vmo;
    if (bootfs.Open(path + 6, &vmo, out_size) != ZX_OK) {
        return ZX_HANDLE_INVALID;
    }
    return vmo.release();
}

zx_status_t devmgr_launch_load(void* ctx, launchpad_t* lp, const char* file) {
    zx_handle_t vmo = devmgr_load_file(file, nullptr);
    if (vmo != ZX_HANDLE_INVALID) {
        return launchpad_load_from_vmo(lp, vmo);
    } else {
        // TODO(mcgrathr): This case is probably never used.  Remove it later.
        return launchpad_load_from_file(lp, file);
    }
}

void devmgr_vfs_exit() {
    zx_status_t status;
    if ((status = fshost_event.signal(0, FSHOST_SIGNAL_EXIT)) != ZX_OK) {
        printf("devmgr: Failed to signal VFS exit\n");
        return;
    } else if ((status = fshost_event.wait_one(FSHOST_SIGNAL_EXIT_DONE,
                                               zx::deadline_after(zx::sec(5)), nullptr)) != ZX_OK) {
        printf("devmgr: Failed to wait for VFS exit completion\n");
    }
}

zx::channel fs_clone(const char* path) {
    if (!strcmp(path, "dev")) {
        return devfs_root_clone();
    }
    zx::channel h0, h1;
    if (zx::channel::create(0, &h0, &h1) != ZX_OK) {
        return zx::channel();
    }
    zx::unowned_channel fs(fs_root);
    int flags = FS_DIR_FLAGS;
    if (!strcmp(path, "hub")) {
        fs = zx::unowned_channel(appmgr_req_cli);
    } else if (!strcmp(path, "svc")) {
        flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE;
        fs = zx::unowned_channel(svchost_outgoing);
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

void devmgr_vfs_init() {
    printf("devmgr: vfs init\n");

    fshost_start();

    fdio_ns_t* ns;
    zx_status_t r;
    if ((r = fdio_ns_create(&ns)) != ZX_OK) {
        printf("devmgr: cannot create namespace: %d\n", r);
        return;
    }
    if ((r = fdio_ns_bind(ns, "/dev", fs_clone("dev").release())) != ZX_OK) {
        printf("devmgr: cannot bind /dev to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/boot", fs_clone("boot").release())) != ZX_OK) {
        printf("devmgr: cannot bind /boot to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/system", fs_clone("system").release())) != ZX_OK) {
        printf("devmgr: cannot bind /system to namespace: %d\n", r);
    }
    if ((r = fdio_ns_install(ns)) != ZX_OK) {
        printf("devmgr: cannot install namespace: %d\n", r);
    }
}

zx_status_t svchost_start() {
    zx::channel dir_request;
    zx::debuglog logger;
    zx::channel appmgr_svc_req;
    zx::channel appmgr_svc;

    zx_status_t status = zx::channel::create(0, &dir_request, &svchost_outgoing);
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

    status = fdio_service_connect_at(appmgr_req_cli.get(), "svc", appmgr_svc_req.release());
    if (status != ZX_OK) {
        return status;
    }

    const char* name = "svchost";
    const char* argv[2] = {
        "/boot/bin/svchost",
        require_system ? "--require-system" : nullptr,
    };
    int argc = require_system ? 2 : 1;

    zx_handle_t svchost_vmo = devmgr_load_file(argv[0], nullptr);
    if (svchost_vmo == ZX_HANDLE_INVALID) {
        // TODO(kulakowski/teisenbe) Synthesize a status here.
        return status;
    }

    zx::job job_copy;
    svcs_job_handle.duplicate(ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MANAGE_JOB, &job_copy);

    launchpad_t* lp = nullptr;
    launchpad_create(job_copy.get(), name, &lp);
    launchpad_load_from_vmo(lp, svchost_vmo);
    launchpad_set_args(lp, argc, argv);
    launchpad_add_handle(lp, dir_request.release(), PA_DIRECTORY_REQUEST);
    launchpad_add_handle(lp, logger.release(), PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO));

    // Remove once svchost hosts the tracelink serice itself.
    launchpad_add_handle(lp, appmgr_svc.release(), PA_HND(PA_USER0, 0));

    const char* errmsg = nullptr;
    if ((status = launchpad_go(lp, nullptr, &errmsg)) < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n",
               argv[0], name, errmsg, status);
    } else {
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
    return ZX_OK;
}

void devmgr_svc_init() {
    printf("devmgr: svc init\n");

    svchost_start();
}

} // namespace devmgr
