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

#include <launchpad/launchpad.h>
#include <launchpad/loader-service.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <fdio/namespace.h>
#include <fdio/util.h>

#include "devmgr.h"
#include "memfs-private.h"

// The handle used to transmit messages to appmgr.
static zx_handle_t svc_root_handle;
// The handle used by appmgr to serve incoming requests.
// If appmgr cannot be launched within a timeout, this handle is closed.
static zx_handle_t svc_request_handle;

zx_handle_t svc_root_clone(void) {
    return fdio_service_clone(svc_root_handle);
}

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

static const char* argv_sh[] = { "/boot/bin/sh" };
static const char* argv_autorun0[] = { "/boot/bin/sh", "/boot/autorun" };
static const char* argv_appmgr[] = { "/system/bin/appmgr" };

void do_autorun(const char* name, const char* env) {
    char* bin = getenv(env);
    if (bin) {
        printf("devmgr: %s: starting %s...\n", env, bin);
        devmgr_launch(svcs_job_handle, name,
                      1, (const char* const*) &bin,
                      NULL, -1, NULL, NULL, 0, NULL);
    }
}

static mtx_t appmgr_lock = MTX_INIT;

int devmgr_start_appmgr(void* arg) {
    static bool appmgr_started = false;
    static bool autorun_started = false;

    // we're starting the appmgr because /system is present
    // so we also signal the device coordinator that those
    // drivers are now loadable
    load_system_drivers();

    mtx_lock(&appmgr_lock);
    struct stat s;
    if (!appmgr_started && stat(argv_appmgr[0], &s) == 0) {
        unsigned int appmgr_hnd_count = 0;
        zx_handle_t appmgr_hnds[2] = {};
        uint32_t appmgr_ids[2] = {};
        if (svc_request_handle) {
            assert(appmgr_hnd_count < countof(appmgr_hnds));
            appmgr_hnds[appmgr_hnd_count] = svc_request_handle;
            appmgr_ids[appmgr_hnd_count] = PA_SERVICE_REQUEST;
            appmgr_hnd_count++;
            svc_request_handle = ZX_HANDLE_INVALID;
        }
        devmgr_launch(fuchsia_job_handle, "appmgr", countof(argv_appmgr),
                      argv_appmgr, NULL, -1, appmgr_hnds, appmgr_ids,
                      appmgr_hnd_count, NULL);
        appmgr_started = true;
    }
    if (!autorun_started) {
        do_autorun("autorun:system", "zircon.autorun.system");
        autorun_started = true;
    }
    mtx_unlock(&appmgr_lock);
    return 0;
}

int service_timeout(void* arg) {
    zx_nanosleep(zx_deadline_after(ZX_SEC(10)));
    mtx_lock(&appmgr_lock);
    if (svc_request_handle != ZX_HANDLE_INVALID) {
        printf("devmgr: appmgr not found, closing service handle\n");
        zx_handle_close(svc_request_handle);
    }
    mtx_unlock(&appmgr_lock);
    return 0;
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

    // Start crashlogger.
    if (!getenv_bool("crashlogger.disable", false)) {
        static const char* argv_crashlogger[] = {
            "/boot/bin/crashlogger",
            NULL,  // room for -pton
        };
        const char* crashlogger_pt = getenv("crashlogger.pt");
        int argc_crashlogger = 1;
        if (crashlogger_pt && strcmp(crashlogger_pt, "true") == 0) {
            // /dev/misc/intel-pt may not be available yet, so we can't
            // actually turn on PT here. Just tell crashlogger to dump the
            // trace buffers if they're available.
            argv_crashlogger[argc_crashlogger++] = "-pton";
        }

        // Bind the exception port now, to avoid missing any crashes that
        // might occur early on before the crashlogger process has finished
        // initializing.
        zx_handle_t exception_port;
        // This should match the value used by crashlogger.
        const uint64_t kSysExceptionKey = 1166444u;
        if (zx_port_create(0, &exception_port) == ZX_OK &&
            zx_task_bind_exception_port(ZX_HANDLE_INVALID, exception_port,
                                        kSysExceptionKey, 0) == ZX_OK) {
            zx_handle_t handles[] = { exception_port };
            uint32_t handle_types[] = { PA_HND(PA_USER0, 0) };

            devmgr_launch(svcs_job_handle, "crashlogger",
                          argc_crashlogger, argv_crashlogger,
                          NULL, -1, handles, handle_types,
                          countof(handles), NULL);
        }
    }

    if (secondary_bootfs_ready()) {
        devmgr_start_appmgr(NULL);
    }

    char vcmd[64];
    bool netboot = false;
    bool vruncmd = false;

    if (!getenv_bool("netsvc.disable", false)) {
        const char* args[] = { "/boot/bin/netsvc", NULL, NULL };
        int argc = 1;

        if (getenv_bool("netsvc.netboot", false)) {
            args[argc++] = "--netboot";
            netboot = true;
            vruncmd = true;
        }

        const char* nodename = getenv("zircon.nodename");
        if (nodename) {
            args[argc++] = nodename;
        }

        zx_handle_t proc;
        if (devmgr_launch(svcs_job_handle, "netsvc", argc, args,
                          NULL, -1, NULL, NULL, 0, &proc) == ZX_OK) {
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

        uint32_t type = PA_HND(PA_USER0, 0);
        zx_handle_t h = ZX_HANDLE_INVALID;
        zx_channel_create(0, &h, &virtcon_open);
        const char* args[] = { "/boot/bin/virtual-console", "--run", vcmd };
        devmgr_launch(svcs_job_handle, "virtual-console",
                      vruncmd ? 3 : 1, args, envp, -1,
                      &h, &type, (h == ZX_HANDLE_INVALID) ? 0 : 1, NULL);
    }


    do_autorun("autorun:boot", "zircon.autorun.boot");
    struct stat s;
    if (stat(argv_autorun0[1], &s) == 0) {
        printf("devmgr: starting /boot/autorun ...\n");
        devmgr_launch(svcs_job_handle, "sh:autorun0",
                      countof(argv_autorun0), argv_autorun0,
                      NULL, -1, NULL, NULL, 0, NULL);
    }

    if (!netboot) {
#ifndef WITH_FSHOST
        block_device_watcher(svcs_job_handle);
#endif
    }
    return 0;
}

#if !_ZX_KERNEL_HAS_SHELL
static int console_starter(void* arg) {
    // if no kernel shell on serial uart, start a sh there
    printf("devmgr: shell startup\n");

    // If we got a TERM environment variable (aka a TERM=... argument on
    // the kernel command line), pass this down.
    const char* term = getenv("TERM");
    if (term != NULL)
        term -= sizeof("TERM=") - 1;

    const char* envp[] = { term ? term : NULL, NULL, };
    for (unsigned n = 0; n < 30; n++) {
        int fd;
        if ((fd = open("/dev/misc/console", O_RDWR)) >= 0) {
            devmgr_launch(svcs_job_handle, "sh:console",
                          countof(argv_sh), argv_sh, envp, fd, NULL, NULL, 0, NULL);
            break;
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    }
    return 0;
}

static void start_console_shell(void) {
    thrd_t t;
    if ((thrd_create_with_name(&t, console_starter, NULL, "console-starter")) == thrd_success) {
        thrd_detach(t);
    }
}
#else
static void start_console_shell(void) {}
#endif

static void load_cmdline_from_bootfs(void) {
    int fd = open("/boot/config/devmgr", O_RDONLY);
    if (fd < 0) {
        return;
    }
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char* cfg;
    if ((sz < 0) || ((cfg = malloc(sz + 1)) == NULL)) {
        close(fd);
        return;
    }
    char* x = cfg;
    while (sz > 0) {
        int r = read(fd, x, sz);
        if (r <= 0) {
            close(fd);
            free(cfg);
            return;
        }
        x += r;
        sz -= r;
    }
    *x = 0;
    close(fd);

    x = cfg;
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
            for (char *y = x; *y != 0; y++) {
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

int main(int argc, char** argv) {
    // Close the loader-service channel so the service can go away.
    // We won't use it any more (no dlopen calls in this process).
    zx_handle_t loader_svc = dl_set_loader_service(ZX_HANDLE_INVALID);
    zx_handle_close(loader_svc);

    // Ensure that devmgr doesn't try to connect to the global
    // loader sevice (as this leads to deadlocks in devhost v2)
    loader_service_force_local();

    devmgr_io_init();

    root_resource_handle = zx_get_startup_handle(PA_HND(PA_RESOURCE, 0));
    root_job_handle = zx_job_default();

    printf("devmgr: main()\n");

    devfs_init(root_job_handle);

    zx_object_set_property(root_job_handle, ZX_PROP_NAME, "root", 4);

    zx_status_t status = zx_job_create(root_job_handle, 0u, &svcs_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    zx_object_set_property(svcs_job_handle, ZX_PROP_NAME, "zircon-services", 16);

    status = zx_job_create(root_job_handle, 0u, &fuchsia_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    zx_object_set_property(fuchsia_job_handle, ZX_PROP_NAME, "fuchsia", 7);

    zx_channel_create(0, &svc_root_handle, &svc_request_handle);

    devmgr_vfs_init();

    load_cmdline_from_bootfs();

    char** e = environ;
    while (*e) {
        printf("cmdline: %s\n", *e++);
    }

    start_console_shell();

    thrd_t t;
    if ((thrd_create_with_name(&t, service_starter, NULL, "service-starter")) == thrd_success) {
        thrd_detach(t);
    }

    if ((thrd_create_with_name(&t, service_timeout, NULL, "service-timout")) == thrd_success) {
        thrd_detach(t);
    }

    coordinator();
    printf("devmgr: coordinator exited?!\n");
    return 0;
}

#ifdef WITH_FSHOST
void fshost_start(void) {
}

zx_handle_t fs_root_clone(void) {
    return ZX_HANDLE_INVALID;
}

void devmgr_vfs_exit(void) {
}

bool secondary_bootfs_ready(void) {
    return false;
}
#endif

void devmgr_vfs_init(void) {
    printf("devmgr: vfs init\n");

    fshost_start();

    fdio_ns_t* ns;
    zx_status_t r;
    if ((r = fdio_ns_create(&ns)) != ZX_OK) {
        printf("devmgr: cannot create namespace: %d\n", r);
        return;
    }
    if ((r = fdio_ns_bind(ns, "/", fs_root_clone())) != ZX_OK) {
        printf("devmgr: cannot bind / to namespace: %d\n", r);
    }
    if ((r = fdio_ns_bind(ns, "/dev", devfs_root_clone())) != ZX_OK) {
        printf("devmgr: cannot bind /dev to namespace: %d\n", r);
    }
    if ((r = fdio_ns_install(ns)) != ZX_OK) {
        printf("devmgr: cannot install namespace: %d\n", r);
    }
}

