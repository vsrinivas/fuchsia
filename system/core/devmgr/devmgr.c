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
#include <magenta/dlfcn.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <magenta/status.h>

#include <mxio/util.h>

#include "devmgr.h"
#include "memfs-private.h"

// When adding VMOs to the boot filesystem, add them under the directory
// /boot/VMO_SUBDIR. This constant must end, but not start, with a slash.
#define VMO_SUBDIR "kernel/"
#define VMO_SUBDIR_LEN (sizeof(VMO_SUBDIR) - 1)

// The handle used to transmit messages to appmgr.
static mx_handle_t svc_root_handle;
// The handle used by appmgr to serve incoming requests.
// If appmgr cannot be launched within a timeout, this handle is closed.
static mx_handle_t svc_request_handle;

mx_handle_t get_service_root(void) {
    return mxio_service_clone(svc_root_handle);
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

static mx_handle_t root_resource_handle;
static mx_handle_t root_job_handle;
static mx_handle_t svcs_job_handle;
static mx_handle_t fuchsia_job_handle;

mx_handle_t virtcon_open;

mx_handle_t get_root_resource(void) {
    return root_resource_handle;
}

mx_handle_t get_sysinfo_job_root(void) {
    mx_handle_t h;
    //TODO: limit to enumerate rights
    if (mx_handle_duplicate(root_job_handle, MX_RIGHT_SAME_RIGHTS, &h) < 0) {
        return MX_HANDLE_INVALID;
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
        mx_handle_t appmgr_hnds[2] = {};
        uint32_t appmgr_ids[2] = {};
        if (svc_request_handle) {
            assert(appmgr_hnd_count < countof(appmgr_hnds));
            appmgr_hnds[appmgr_hnd_count] = svc_request_handle;
            appmgr_ids[appmgr_hnd_count] = PA_SERVICE_REQUEST;
            appmgr_hnd_count++;
            svc_request_handle = MX_HANDLE_INVALID;
        }
        devmgr_launch(fuchsia_job_handle, "appmgr", countof(argv_appmgr),
                      argv_appmgr, NULL, -1, appmgr_hnds, appmgr_ids,
                      appmgr_hnd_count, NULL);
        appmgr_started = true;
    }
    if (!autorun_started) {
        do_autorun("autorun:system", "magenta.autorun.system");
        autorun_started = true;
    }
    mtx_unlock(&appmgr_lock);
    return 0;
}

int service_timeout(void* arg) {
    mx_nanosleep(mx_deadline_after(MX_SEC(10)));
    mtx_lock(&appmgr_lock);
    if (svc_request_handle != MX_HANDLE_INVALID) {
        printf("devmgr: appmgr not found, closing service handle\n");
        mx_handle_close(svc_request_handle);
    }
    mtx_unlock(&appmgr_lock);
    return 0;
}

int service_starter(void* arg) {
    // create a directory for sevice rendezvous
    mkdir("/svc", 0755);

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

        const char* nodename = getenv("magenta.nodename");
        if (nodename) {
            args[argc++] = nodename;
        }

        mx_handle_t proc;
        if (devmgr_launch(svcs_job_handle, "netsvc", argc, args,
                          NULL, -1, NULL, NULL, 0, &proc) == MX_OK) {
            if (vruncmd) {
                mx_info_handle_basic_t info = {
                    .koid = 0,
                };
                mx_object_get_info(proc, MX_INFO_HANDLE_BASIC,
                                   &info, sizeof(info), NULL, NULL);
                mx_handle_close(proc);
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
        mx_handle_t h = MX_HANDLE_INVALID;
        mx_channel_create(0, &h, &virtcon_open);
        const char* args[] = { "/boot/bin/virtual-console", "--run", vcmd };
        devmgr_launch(svcs_job_handle, "virtual-console",
                      vruncmd ? 3 : 1, args, envp, -1,
                      &h, &type, (h == MX_HANDLE_INVALID) ? 0 : 1, NULL);
    }


    do_autorun("autorun:boot", "magenta.autorun.boot");
    struct stat s;
    if (stat(argv_autorun0[1], &s) == 0) {
        printf("devmgr: starting /boot/autorun ...\n");
        devmgr_launch(svcs_job_handle, "sh:autorun0",
                      countof(argv_autorun0), argv_autorun0,
                      NULL, -1, NULL, NULL, 0, NULL);
    }

    if (!netboot) {
        block_device_watcher(svcs_job_handle);
    }
    return 0;
}

#if !_MX_KERNEL_HAS_SHELL
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
        mx_nanosleep(mx_deadline_after(MX_MSEC(100)));
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

// Look for VMOs passed as startup handles of PA_HND_TYPE type, and add them to
// the filesystem under the path /boot/VMO_SUBDIR_LEN/<vmo-name>.
static void fetch_vmos(uint_fast8_t type, const char* debug_type_name) {
    for (uint_fast16_t i = 0; true; ++i) {
        mx_handle_t vmo = mx_get_startup_handle(PA_HND(type, i));
        if (vmo == MX_HANDLE_INVALID)
            break;

        if (type == PA_VMO_VDSO && i == 0) {
            // The first vDSO is the default vDSO.  Since we've stolen
            // the startup handle, launchpad won't find it on its own.
            // So point launchpad at it.
            launchpad_set_vdso_vmo(vmo);
        }

        // The vDSO VMOs have names like "vdso/default", so those
        // become VMO files at "/boot/kernel/vdso/default".
        char name[VMO_SUBDIR_LEN + MX_MAX_NAME_LEN] = VMO_SUBDIR;
        size_t size;
        mx_status_t status = mx_object_get_property(vmo, MX_PROP_NAME,
                name + VMO_SUBDIR_LEN, sizeof(name) - VMO_SUBDIR_LEN);
        if (status != MX_OK) {
            printf("devmgr: mx_object_get_property on %s %u: %s\n",
                   debug_type_name, i, mx_status_get_string(status));
            continue;
        }
        status = mx_vmo_get_size(vmo, &size);
        if (status != MX_OK) {
            printf("devmgr: mx_vmo_get_size on %s %u: %s\n",
                   debug_type_name, i, mx_status_get_string(status));
            continue;
        }
        if (size == 0) {
            // empty vmos do not get installed
            mx_handle_close(vmo);
            continue;
        }
        if (!strcmp(name + VMO_SUBDIR_LEN, "crashlog")) {
            // the crashlog has a special home
            strcpy(name, "log/last-panic.txt");
        }
        status = bootfs_add_file(name, vmo, 0, size);
        if (status != MX_OK) {
            printf("devmgr: failed to add %s %u to filesystem: %s\n",
                   debug_type_name, i, mx_status_get_string(status));
        }
    }
}

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
    mx_handle_t loader_svc = dl_set_loader_service(MX_HANDLE_INVALID);
    mx_handle_close(loader_svc);

    // Ensure that devmgr doesn't try to connect to the global
    // loader sevice (as this leads to deadlocks in devhost v2)
    loader_service_force_local();

    devmgr_io_init();

    root_resource_handle = mx_get_startup_handle(PA_HND(PA_RESOURCE, 0));
    root_job_handle = mx_job_default();

    printf("devmgr: main()\n");

    devmgr_init(root_job_handle);
    devmgr_vfs_init();

    load_cmdline_from_bootfs();

    char** e = environ;
    while (*e) {
        printf("cmdline: %s\n", *e++);
    }

    mx_object_set_property(root_job_handle, MX_PROP_NAME, "root", 4);

    fetch_vmos(PA_VMO_VDSO, "PA_VMO_VDSO");
    fetch_vmos(PA_VMO_KERNEL_FILE, "PA_VMO_KERNEL_FILE");


    mx_status_t status = mx_job_create(root_job_handle, 0u, &svcs_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    mx_object_set_property(svcs_job_handle, MX_PROP_NAME, "magenta-services", 16);

    status = mx_job_create(root_job_handle, 0u, &fuchsia_job_handle);
    if (status < 0) {
        printf("unable to create service job\n");
    }
    mx_object_set_property(fuchsia_job_handle, MX_PROP_NAME, "fuchsia", 7);

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
        mx_handle_t exception_port;
        // This should match the value used by crashlogger.
        const uint64_t kSysExceptionKey = 1166444u;
        if (mx_port_create(0, &exception_port) == MX_OK &&
            mx_task_bind_exception_port(MX_HANDLE_INVALID, exception_port,
                                        kSysExceptionKey, 0) == MX_OK) {
            mx_handle_t handles[] = { exception_port };
            uint32_t handle_types[] = { PA_HND(PA_USER0, 0) };

            devmgr_launch(svcs_job_handle, "crashlogger",
                          argc_crashlogger, argv_crashlogger,
                          NULL, -1, handles, handle_types,
                          countof(handles), NULL);
        }
    }

    mx_channel_create(0, &svc_root_handle, &svc_request_handle);

    start_console_shell();

    if (secondary_bootfs_ready()) {
        devmgr_start_appmgr(NULL);
    }

    thrd_t t;
    if ((thrd_create_with_name(&t, service_starter, NULL, "service-starter")) == thrd_success) {
        thrd_detach(t);
    }

    if ((thrd_create_with_name(&t, service_timeout, NULL, "service-timout")) == thrd_success) {
        thrd_detach(t);
    }

    devmgr_handle_messages();
    printf("devmgr: message handler returned?!\n");
    return 0;
}
