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

#include "bootfs.h"
#include "devmgr.h"
#include "memfs-private.h"

// Global flag tracking if devmgr believes this is a full Fuchsia build
// (requiring /system, etc) or not.
bool require_system;

// The handle used to transmit messages to appmgr.
static zx_handle_t appmgr_req_cli;

// The handle used by appmgr to serve incoming requests.
// If appmgr cannot be launched within a timeout, this handle is closed.
static zx_handle_t appmgr_req_srv;

bool getenv_bool(const char* key, bool _default) {
    const char* value = getenv(key);
    if (value == NULL) {
        return _default;
    }
    if ((strcmp(value, "0") == 0) ||
        (strcmp(value, "false") == 0) ||
        (strcmp(value, "off") == 0)) {
        return false;
    }
    return true;
}

static zx_handle_t root_resource_handle;
static zx_handle_t root_job_handle;
static zx_handle_t svcs_job_handle;
static zx_handle_t fuchsia_job_handle;
static zx_handle_t exception_channel;
static zx_handle_t svchost_outgoing;

zx_handle_t virtcon_open;

zx_handle_t get_root_resource(void) {
    return root_resource_handle;
}

zx_handle_t get_sysinfo_job_root(void) {
    zx_handle_t h;
    //TODO: limit to enumerate rights
    if (zx_handle_duplicate(root_job_handle, ZX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return ZX_HANDLE_INVALID;
    } else {
        return h;
    }
}

static const char* argv_sh[] = {"/boot/bin/sh"};
static const char* argv_appmgr[] = {"/system/bin/appmgr"};

void do_autorun(const char* name, const char* env) {
    const char* cmd = getenv(env);
    if (cmd != NULL) {
        devmgr_launch_cmdline(env, svcs_job_handle, name,
                              &devmgr_launch_load, NULL, cmd,
                              NULL, NULL, 0, NULL, FS_ALL);
    }
}

static zx_handle_t fshost_event;

static int fuchsia_starter(void* arg) {
    bool appmgr_started = false;
    bool autorun_started = false;
    bool drivers_loaded = false;

    zx_time_t deadline = zx_deadline_after(ZX_SEC(10));

    do {
        zx_status_t status = zx_object_wait_one(fshost_event, FSHOST_SIGNAL_READY, deadline, NULL);
        if (status == ZX_ERR_TIMED_OUT) {
            if (appmgr_req_srv != ZX_HANDLE_INVALID) {
                if (require_system) {
                    printf("devmgr: appmgr not launched in 10s, closing appmgr handle\n");
                }
                zx_handle_close(appmgr_req_srv);
            }
            deadline = ZX_TIME_INFINITE;
            continue;
        }
        if (status != ZX_OK) {
            printf("devmgr: error waiting on fuchsia start event: %d\n", status);
            break;
        }
        zx_object_signal(fshost_event, FSHOST_SIGNAL_READY, 0);

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
            if (appmgr_req_srv) {
                assert(appmgr_hnd_count < countof(appmgr_hnds));
                appmgr_hnds[appmgr_hnd_count] = appmgr_req_srv;
                appmgr_ids[appmgr_hnd_count] = PA_DIRECTORY_REQUEST;
                appmgr_hnd_count++;
                appmgr_req_srv = ZX_HANDLE_INVALID;
            }
            devmgr_launch(fuchsia_job_handle, "appmgr",
                          &devmgr_launch_load, NULL,
                          countof(argv_appmgr), argv_appmgr, NULL, -1,
                          appmgr_hnds, appmgr_ids, appmgr_hnd_count,
                          NULL, FS_FOR_APPMGR);
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
            zx_object_wait_one(exception_channel, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                               ZX_TIME_INFINITE, &observed);
        if (status != ZX_OK) {
            printf("devmgr: crash_analyzer_listener zx_object_wait_one failed: %d\n", status);
            return 1;
        }
        if ((observed & ZX_CHANNEL_READABLE) == 0) {
            printf("devmgr: crash_analyzer_listener: peer closed\n");
            return 1;
        }

        uint32_t exception_type;
        zx_handle_t handles[2];
        uint32_t actual_bytes, actual_handles;
        status =
            zx_channel_read(exception_channel, 0, &exception_type, handles, sizeof(exception_type),
                            countof(handles), &actual_bytes, &actual_handles);
        if (status != ZX_OK) {
            printf("devmgr: zx_channel_read failed: %d\n", status);
            continue;
        }
        if (actual_bytes != sizeof(exception_type) || actual_handles != countof(handles)) {
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
            zx_handle_close(handles[0]);
            zx_handle_close(handles[1]);
            // Shouldn't we resume handles[1] in this case?
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

        const char* analyzer_command = getenv("crashsvc.analyzer");
        if (analyzer_command && strcmp(analyzer_command, "from-appmgr") == 0) {
            // TODO(abarth|scottmg): Appmgr appears to fail at lookups
            // containing /, so do lookup in two steps ("svc", then "Analyzer")
            // for now. ZX-2265.
            status = zx_channel_create(0, &appmgr_svc_request, &appmgr_svc);
            if (status != ZX_OK)
                goto cleanup;
            status = fdio_service_connect_at(appmgr_req_cli, "svc", appmgr_svc_request);
            if (status != ZX_OK)
                goto cleanup;
            appmgr_svc_request = ZX_HANDLE_INVALID;
            status = fdio_service_connect_at(appmgr_svc, fuchsia_crash_Analyzer_Name, analyzer_request);
        } else {
            status = fdio_service_connect_at(svchost_outgoing, "public/" fuchsia_crash_Analyzer_Name, analyzer_request);
        }
        analyzer_request = ZX_HANDLE_INVALID;
        if (status != ZX_OK)
            goto cleanup;
        status = fuchsia_crash_AnalyzerAnalyze(analyzer, handles[0], handles[1]);
        // fuchsia_crash_AnalyzerAnalyze always consumes the handles.
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
    zx_handle_t exception_port, exception_channel_passed;
    if (zx_port_create(0, &exception_port) == ZX_OK &&
        zx_channel_create(0, &exception_channel, &exception_channel_passed) == ZX_OK &&
        zx_task_bind_exception_port(root_job_handle, exception_port, 0, 0) == ZX_OK) {
        thrd_t t;
        if ((thrd_create_with_name(&t, crash_analyzer_listener, NULL,
                                   "crash-analyzer-listener")) == thrd_success) {
            thrd_detach(t);
        }
        zx_handle_t handles[] = {ZX_HANDLE_INVALID, exception_port, exception_channel_passed};
        zx_handle_duplicate(root_job_handle, ZX_RIGHT_SAME_RIGHTS, &handles[0]);
        uint32_t handle_types[] = {PA_HND(PA_USER0, 0), PA_HND(PA_USER0, 1), PA_HND(PA_USER0, 2)};
        static const char* argv_crashsvc[] = {"/boot/bin/crashsvc"};
        devmgr_launch(svcs_job_handle, "crashsvc",
                      &devmgr_launch_load, NULL,
                      countof(argv_crashsvc), argv_crashsvc, NULL, -1,
                      handles, handle_types, countof(handles), NULL, 0);
    }

    char vcmd[64];
    __UNUSED bool netboot = false;
    bool vruncmd = false;
    if (!getenv_bool("netsvc.disable", false)) {
        const char* args[] = {"/boot/bin/netsvc", NULL, NULL, NULL, NULL, NULL};
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
        if ((interface = getenv("netsvc.interface")) != NULL) {
            args[argc++] = "--interface";
            args[argc++] = interface;
        }

        const char* nodename = getenv("zircon.nodename");
        if (nodename) {
            args[argc++] = nodename;
        }

        zx_handle_t proc;
        if (devmgr_launch(svcs_job_handle, "netsvc",
                          &devmgr_launch_load, NULL, argc, args,
                          NULL, -1, NULL, NULL, 0, &proc, FS_ALL) == ZX_OK) {
            if (vruncmd) {
                zx_info_handle_basic_t info = {
                    .koid = 0,
                };
                zx_object_get_info(proc, ZX_INFO_HANDLE_BASIC,
                                   &info, sizeof(info), NULL, NULL);
                zx_handle_close(proc);
                snprintf(vcmd, sizeof(vcmd), "dlog -f -t -p %zu", info.koid);
            }
        } else {
            vruncmd = false;
        }
    }

    if (!getenv_bool("virtcon.disable", false)) {
        // pass virtcon.* options along
        const char* envp[16];
        unsigned envc = 0;
        char** e = environ;
        while (*e && (envc < countof(envp))) {
            if (!strncmp(*e, "virtcon.", 8)) {
                envp[envc++] = *e;
            }
            e++;
        }
        envp[envc] = NULL;

        const char* num_shells = require_system && !netboot ? "0" : "3";

        uint32_t type = PA_HND(PA_USER0, 0);
        zx_handle_t h = ZX_HANDLE_INVALID;
        zx_channel_create(0, &h, &virtcon_open);
        const char* args[] = {"/boot/bin/virtual-console", "--shells", num_shells, "--run", vcmd};
        devmgr_launch(svcs_job_handle, "virtual-console",
                      &devmgr_launch_load, NULL,
                      vruncmd ? 5 : 3, args, envp, -1,
                      &h, &type, (h == ZX_HANDLE_INVALID) ? 0 : 1, NULL, FS_ALL);
    }

    const char* epoch = getenv("devmgr.epoch");
    if (epoch) {
        zx_time_t offset = ZX_SEC(atoi(epoch));
        zx_clock_adjust(get_root_resource(), ZX_CLOCK_UTC, offset);
    }

    do_autorun("autorun:boot", "zircon.autorun.boot");

    thrd_t t;
    if ((thrd_create_with_name(&t, fuchsia_starter, NULL, "fuchsia-starter")) == thrd_success) {
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
    if (term == NULL) {
        term = "TERM=uart";
    } else {
        term -= sizeof("TERM=") - 1;
    }

    const char* device = getenv("console.path");
    if (!device)
        device = "/dev/misc/console";

    const char* envp[] = {
        term,
        NULL,
    };
    for (unsigned n = 0; n < 30; n++) {
        int fd;
        if ((fd = open(device, O_RDWR)) >= 0) {
            devmgr_launch(svcs_job_handle, "sh:console",
                          &devmgr_launch_load, NULL,
                          countof(argv_sh), argv_sh, envp, fd, NULL, NULL, 0, NULL, FS_ALL);
            break;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    }
    return 0;
}

static void start_console_shell(void) {
    // start a shell on the kernel console if it isn't already running a shell
    if (!getenv_bool("kernel.shell", false)) {
        thrd_t t;
        if ((thrd_create_with_name(&t, console_starter, NULL, "console-starter")) == thrd_success) {
            thrd_detach(t);
        }
    }
}

static void load_cmdline_from_bootfs(void) {
    uint32_t file_size;
    zx_handle_t vmo = devmgr_load_file("/boot/config/devmgr", &file_size);
    if (vmo == ZX_HANDLE_INVALID) {
        return;
    }

    char* cfg = malloc(file_size + 1);
    if (cfg == NULL) {
        zx_handle_close(vmo);
        return;
    }

    zx_status_t status = zx_vmo_read(vmo, cfg, 0, file_size);
    if (status != ZX_OK) {
        printf("zx_vmo_read on /boot/config/devmgr BOOTFS VMO: %d (%s)\n",
               status, zx_status_get_string(status));
        free(cfg);
        return;
    }
    cfg[file_size] = '\0';

    char* x = cfg;
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

static zx_status_t fuchsia_create_job(void) {
    zx_status_t status = zx_job_create(root_job_handle, 0u, &fuchsia_job_handle);
    if (status != ZX_OK) {
        printf("devmgr: unable to create fuchsia job: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }

    zx_object_set_property(fuchsia_job_handle, ZX_PROP_NAME, "fuchsia", 7);

    const zx_policy_basic_t fuchsia_job_policy[] = {
        {.condition = ZX_POL_NEW_PROCESS, .policy = ZX_POL_ACTION_DENY}};

    status = zx_job_set_policy(fuchsia_job_handle, ZX_JOB_POL_RELATIVE, ZX_JOB_POL_BASIC,
                               fuchsia_job_policy, countof(fuchsia_job_policy));
    if (status != ZX_OK) {
        printf("devmgr: unable to set policy fuchsia job: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }

    return ZX_OK;
}

int main(int argc, char** argv) {
    // Close the loader-service channel so the service can go away.
    // We won't use it any more (no dlopen calls in this process).
    zx_handle_close(dl_set_loader_service(ZX_HANDLE_INVALID));

    devmgr_io_init();

    root_resource_handle = zx_take_startup_handle(PA_HND(PA_RESOURCE, 0));
    root_job_handle = zx_job_default();

    printf("devmgr: main()\n");

    devfs_init(root_job_handle);

    zx_object_set_property(root_job_handle, ZX_PROP_NAME, "root", 4);

    zx_status_t status = zx_job_create(root_job_handle, 0u, &svcs_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    zx_object_set_property(svcs_job_handle, ZX_PROP_NAME, "zircon-services", 16);

    if (fuchsia_create_job() != ZX_OK)
        return 1;

    zx_channel_create(0, &appmgr_req_cli, &appmgr_req_srv);
    zx_event_create(0, &fshost_event);

    bootfs_create_from_startup_handle();

    load_cmdline_from_bootfs();
    char** e = environ;
    while (*e) {
        printf("cmdline: %s\n", *e++);
    }

    devmgr_svc_init();
    devmgr_vfs_init();

    require_system = getenv_bool("devmgr.require-system", false);

    // if we're not a full fuchsia build, no point to set up appmgr services
    // which will just cause things attempting to access it to block until
    // we give up on the appmgr 10s later
    if (!require_system) {
        devmgr_disable_appmgr_services();
    }

    start_console_shell();

    thrd_t t;
    if ((thrd_create_with_name(&t, service_starter, NULL, "service-starter")) == thrd_success) {
        thrd_detach(t);
    }

    coordinator();
    printf("devmgr: coordinator exited?!\n");
    return 0;
}

static zx_handle_t fs_root;

static bootfs_t bootfs;

static zx_status_t load_object(void* ctx, const char* name, zx_handle_t* vmo) {
    char tmp[256];
    if (snprintf(tmp, sizeof(tmp), "lib/%s", name) >= (int)sizeof(tmp)) {
        return ZX_ERR_BAD_PATH;
    }
    bootfs_t* bootfs = ctx;
    return bootfs_open(bootfs, tmp, vmo, NULL);
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
};

static loader_service_t* loader_service;

#define MAXHND ZX_CHANNEL_MAX_MSG_HANDLES

void bootfs_create_from_startup_handle(void) {
    zx_handle_t bootfs_vmo = zx_take_startup_handle(PA_HND(PA_VMO_BOOTFS, 0));
    if ((bootfs_vmo == ZX_HANDLE_INVALID) ||
        (bootfs_create(&bootfs, bootfs_vmo) != ZX_OK)) {
        printf("devmgr: cannot find and open bootfs\n");
        exit(1);
    }

    // create a local loader service backed directly by the primary bootfs
    // to allow us to load the fshost (since we don't have filesystems before
    // the fshost starts up).
    zx_handle_t ldsvc;
    if ((loader_service_create(NULL, &loader_ops, &bootfs, &loader_service) != ZX_OK) ||
        (loader_service_connect(loader_service, &ldsvc) != ZX_OK)) {
        printf("devmgr: cannot create loader service\n");
        exit(1);
    }

    // set the bootfs-loader as the default loader service for now
    zx_handle_close(dl_set_loader_service(ldsvc));
}

void fshost_start(void) {
    // assemble handles to pass down to fshost
    zx_handle_t handles[MAXHND];
    uint32_t types[MAXHND];
    size_t n = 0;
    zx_handle_t ldsvc;

    // pass /, /dev, and /svc handles to fsboot
    if (zx_channel_create(0, &fs_root, &handles[0]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER0, 0);
    }
    if ((handles[n] = devfs_root_clone()) != ZX_HANDLE_INVALID) {
        types[n++] = PA_HND(PA_USER0, 1);
    }
    if ((handles[n] = fs_clone("svc")) != ZX_HANDLE_INVALID) {
        types[n++] = PA_HND(PA_USER0, 2);
    }
    if (zx_channel_create(0, &ldsvc, &handles[n]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER0, 3);
    } else {
        ldsvc = ZX_HANDLE_INVALID;
    }

    // pass primary bootfs to fshost
    if (zx_handle_duplicate(bootfs.vmo, ZX_RIGHT_SAME_RIGHTS, &handles[n]) == ZX_OK) {
        types[n++] = PA_HND(PA_VMO_BOOTFS, 0);
    }

    // pass fuchsia start event to fshost
    if (zx_handle_duplicate(fshost_event, ZX_RIGHT_SAME_RIGHTS, &handles[n]) == ZX_OK) {
        types[n++] = PA_HND(PA_USER1, 0);
    }

    // pass bootdata VMOs to fshost
    for (size_t m = 0; n < MAXHND; m++) {
        uint32_t type = PA_HND(PA_VMO_BOOTDATA, m);
        if ((handles[n] = zx_take_startup_handle(type)) != ZX_HANDLE_INVALID) {
            devmgr_set_bootdata(handles[n]);
            types[n++] = type;
        } else {
            break;
        }
    }

    // pass VDSO VMOS to fshost
    for (size_t m = 0; n < MAXHND; m++) {
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
    for (size_t m = 0; n < MAXHND; m++) {
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
    while (*e && (envc < countof(envp))) {
        if (!strncmp(*e, "zircon.system", strlen("zircon.system"))) {
            envp[envc++] = *e;
        }
        e++;
    }
    envp[envc] = NULL;

    devmgr_launch(svcs_job_handle, "fshost",
                  &devmgr_launch_load, NULL, argc, argv, envp, -1,
                  handles, types, n, NULL, 0);

    // switch to system loader service provided by fshost
    zx_handle_close(dl_set_loader_service(ldsvc));
}

zx_handle_t devmgr_load_file(const char* path, uint32_t* out_size) {
    if (strncmp(path, "/boot/", 6)) {
        return ZX_HANDLE_INVALID;
    }
    zx_handle_t vmo = ZX_HANDLE_INVALID;
    bootfs_open(&bootfs, path + 6, &vmo, out_size);
    return vmo;
}

zx_status_t devmgr_launch_load(void* ctx, launchpad_t* lp, const char* file) {
    zx_handle_t vmo = devmgr_load_file(file, NULL);
    if (vmo != ZX_HANDLE_INVALID) {
        return launchpad_load_from_vmo(lp, vmo);
    } else {
        // TODO(mcgrathr): This case is probably never used.  Remove it later.
        return launchpad_load_from_file(lp, file);
    }
}

void devmgr_vfs_exit(void) {
    zx_status_t status;
    if ((status = zx_object_signal(fshost_event, 0, FSHOST_SIGNAL_EXIT)) != ZX_OK) {
        printf("devmgr: Failed to signal VFS exit\n");
        return;
    } else if ((status = zx_object_wait_one(fshost_event,
                                            FSHOST_SIGNAL_EXIT_DONE,
                                            zx_deadline_after(ZX_SEC(5)), NULL)) != ZX_OK) {
        printf("devmgr: Failed to wait for VFS exit completion\n");
    }
}

zx_handle_t fs_clone(const char* path) {
    if (!strcmp(path, "dev")) {
        return devfs_root_clone();
    }
    zx_handle_t h0, h1;
    if (zx_channel_create(0, &h0, &h1) != ZX_OK) {
        return ZX_HANDLE_INVALID;
    }
    zx_handle_t fs = fs_root;
    int flags = FS_DIR_FLAGS;
    if (!strcmp(path, "hub")) {
        fs = appmgr_req_cli;
    } else if (!strcmp(path, "svc")) {
        flags = ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE;
        fs = svchost_outgoing;
        path = "public";
    }
    if (fdio_open_at(fs, path, flags, h1) != ZX_OK) {
        zx_handle_close(h0);
        return ZX_HANDLE_INVALID;
    }
    return h0;
}

void devmgr_vfs_init(void) {
    printf("devmgr: vfs init\n");

    fshost_start();

    fdio_ns_t* ns;
    zx_status_t r;
    if ((r = fdio_ns_create(&ns)) != ZX_OK) {
        printf("devmgr: cannot create namespace: %d\n", r);
        return;
    }
    if ((r = fdio_ns_bind(ns, "/dev", fs_clone("dev"))) != ZX_OK) {
        printf("devmgr: cannot bind /dev to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/boot", fs_clone("boot"))) != ZX_OK) {
        printf("devmgr: cannot bind /boot to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/system", fs_clone("system"))) != ZX_OK) {
        printf("devmgr: cannot bind /system to namespace: %d\n", r);
    }
    if ((r = fdio_ns_install(ns)) != ZX_OK) {
        printf("devmgr: cannot install namespace: %d\n", r);
    }
}

zx_status_t svchost_start(void) {
    zx_handle_t dir_request = ZX_HANDLE_INVALID;
    zx_handle_t logger = ZX_HANDLE_INVALID;
    zx_handle_t appmgr_svc_req = ZX_HANDLE_INVALID;
    zx_handle_t appmgr_svc = ZX_HANDLE_INVALID;

    zx_status_t status = zx_channel_create(0, &dir_request, &svchost_outgoing);
    if (status != ZX_OK) {
        goto error;
    }

    status = zx_log_create(0, &logger);
    if (status != ZX_OK) {
        goto error;
    }

    status = zx_channel_create(0, &appmgr_svc_req, &appmgr_svc);
    if (status != ZX_OK) {
        goto error;
    }

    status = fdio_service_connect_at(appmgr_req_cli, "svc", appmgr_svc_req);
    if (status != ZX_OK) {
        goto error;
    }

    const char* name = "svchost";
    const char* argv[] = {"/boot/bin/svchost"};

    zx_handle_t svchost_vmo = devmgr_load_file(argv[0], NULL);
    if (svchost_vmo == ZX_HANDLE_INVALID) {
        goto error;
    }

    zx_handle_t job_copy = ZX_HANDLE_INVALID;
    zx_handle_duplicate(svcs_job_handle, ZX_RIGHTS_BASIC | ZX_RIGHTS_IO | ZX_RIGHT_MANAGE_JOB, &job_copy);

    launchpad_t* lp = NULL;
    launchpad_create(job_copy, name, &lp);
    launchpad_load_from_vmo(lp, svchost_vmo);
    launchpad_set_args(lp, 1, argv);
    launchpad_add_handle(lp, dir_request, PA_DIRECTORY_REQUEST);
    launchpad_add_handle(lp, logger, PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO));

    // Remove once svchost hosts the tracelink serice itself.
    launchpad_add_handle(lp, appmgr_svc, PA_HND(PA_USER0, 0));

    zx_handle_t process = ZX_HANDLE_INVALID;
    const char* errmsg = NULL;
    if ((status = launchpad_go(lp, &process, &errmsg)) < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n",
               argv[0], name, errmsg, status);
    } else {
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
    zx_handle_close(job_copy);
    return ZX_OK;

error:
    if (dir_request != ZX_HANDLE_INVALID)
        zx_handle_close(dir_request);
    if (logger != ZX_HANDLE_INVALID)
        zx_handle_close(logger);
    // We don't need to clean up appmgr_svc_req because it is always consumed by
    // fdio_service_connect_at.
    if (appmgr_svc != ZX_HANDLE_INVALID)
        zx_handle_close(appmgr_svc);
    return status;
}

void devmgr_svc_init(void) {
    printf("devmgr: svc init\n");

    svchost_start();
}
