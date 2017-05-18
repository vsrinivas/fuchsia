// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <mxio/loader-service.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void option_usage(FILE* out,
                         const char* option, const char* description) {
    fprintf(out, "\t%-16s%s\n", option, description);
}

static _Noreturn void usage(const char* progname, bool error) {
    FILE* out = error ? stderr : stdout;
    fprintf(out, "Usage: %s [OPTIONS] [--] PROGRAM [ARGS...]\n", progname);
    option_usage(out, "-b", "use basic ELF loading, no PT_INTERP support");
    option_usage(out, "-d FD", "pass FD with the same descriptor number");
    option_usage(out, "-d FD:NEWFD", "pass FD as descriptor number NEWFD");
    option_usage(out, "-e VAR=VALUE", "pass environment variable");
    option_usage(out, "-f FILE", "execute FILE but pass PROGRAM as argv[0]");
    option_usage(out, "-F FD", "execute FD");
    option_usage(out, "-h", "display this usage message and exit");
    option_usage(out, "-j", "start process in a new job");
    option_usage(out, "-l",
                 "pass mxio_loader_service handle in main bootstrap message");
    option_usage(out, "-L", "force initial loader bootstrap message");
    option_usage(out, "-r", "send mxio filesystem root");
    option_usage(out, "-s", "shorthand for -r -d 0 -d 1 -d 2");
    option_usage(out, "-S BYTES", "set the initial stack size to BYTES");
    option_usage(out, "-v FILE", "send VMO of FILE as EXEC_VMO handle");
    option_usage(out, "-V FD", "send VMO of FD as EXEC_VMO handle");
    exit(error ? 1 : 0);
}

static _Noreturn void fail(const char* call, mx_status_t status) {
    fprintf(stderr, "%s failed: %d\n", call, status);
    exit(1);
}

static void check(const char* call, mx_status_t status) {
    if (status < 0)
        fail(call, status);
}

int main(int argc, char** argv) {
    const char** env = NULL;
    size_t envsize = 0;
    const char* program = NULL;
    int program_fd = -1;
    bool basic = false;
    bool send_root = false;
    struct fd { int from, to; } *fds = NULL;
    size_t nfds = 0;
    bool send_loader_message = false;
    bool pass_loader_handle = false;
    bool new_job = false;
    const char* exec_vmo_file = NULL;
    int exec_vmo_fd = -1;
    size_t stack_size = -1;

    for (int opt; (opt = getopt(argc, argv, "bd:e:f:F:hjlLrsS:v:")) != -1;) {
        switch (opt) {
        case 'b':
            basic = true;
            break;
        case 'd':;
            int from, to;
            switch (sscanf(optarg, "%u:%u", &from, &to)) {
            default:
                usage(argv[0], true);
                break;
            case 1:
                to = from;
                // Fall through.
            case 2:
                fds = realloc(fds, ++nfds * sizeof(fds[0]));
                if (fds == NULL) {
                    perror("realloc");
                    return 2;
                }
                fds[nfds - 1].from = from;
                fds[nfds - 1].to = to;
                break;
            }
            break;
        case 'e':
            env = realloc(env, (++envsize + 1) * sizeof(env[0]));
            if (env == NULL) {
                perror("realloc");
                return 2;
            }
            env[envsize - 1] = optarg;
            env[envsize] = NULL;
            break;
        case 'f':
            program = optarg;
            break;
        case 'F':
            if (sscanf(optarg, "%u", &program_fd) != 1)
                usage(argv[0], true);
            break;
        case 'h':
            usage(argv[0], false);
            break;
        case 'j':
            new_job = true;
            break;
        case 'L':
            send_loader_message = true;
            break;
        case 'l':
            pass_loader_handle = true;
            break;
        case 'r':
            send_root = true;
            break;
        case 's':
            send_root = true;
            fds = realloc(fds, (nfds + 3) * sizeof(fds[0]));
            for (int i = 0; i < 3; ++i)
                fds[nfds + i].from = fds[nfds + i].to = i;
            nfds += 3;
            break;
        case 'S':
            if (sscanf(optarg, "0x%zx", &stack_size) != 1 &&
                sscanf(optarg, "0%zo", &stack_size) != 1 &&
                sscanf(optarg, "%zu", &stack_size) != 1)
                usage(argv[0], true);
            break;
        case 'v':
            exec_vmo_file = optarg;
            break;
        case 'V':
            if (sscanf(optarg, "%u", &exec_vmo_fd) != 1)
                usage(argv[0], true);
            break;
        default:
            usage(argv[0], true);
        }
    }

    if (optind >= argc)
        usage(argv[0], true);

    mx_handle_t vmo;
    if (program_fd != -1) {
        vmo = launchpad_vmo_from_fd(program_fd);
        if (vmo == ERR_IO) {
            perror("launchpad_vmo_from_fd");
            return 2;
        }
        check("launchpad_vmo_from_fd", vmo);
    } else {
        if (program == NULL)
            program = argv[optind];
        vmo = launchpad_vmo_from_file(program);
        if (vmo == ERR_IO) {
            perror(program);
            return 2;
        }
        check("launchpad_vmo_from_file", vmo);
    }

    mx_handle_t job = mx_job_default();
    if (new_job) {
        if (job == MX_HANDLE_INVALID) {
            fprintf(stderr, "no mxio job handle found\n");
            return 2;
        }
        check("launchpad job", job);
        mx_handle_t child_job;
        mx_status_t status = mx_job_create(job, 0u, &child_job);
        check("launchpad child job", status);
        mx_handle_close(job);
        job = child_job;
    }

    launchpad_t* lp;
    mx_status_t status = launchpad_create(job, program, &lp);
    check("launchpad_create", status);

    status = launchpad_set_args(lp, argc - optind,
                                (const char *const*) &argv[optind]);
    check("launchpad_arguments", status);

    status = launchpad_set_environ(lp, env);
    check("launchpad_environ", status);

    if (send_root) {
        status = launchpad_clone(lp, LP_CLONE_MXIO_ROOT);
        check("launchpad_clone_mxio_root", status);
    }

    for (size_t i = 0; i < nfds; ++i) {
        status = launchpad_clone_fd(lp, fds[i].from, fds[i].to);
        check("launchpad_clone_fd", status);
    }

    if (basic) {
        status = launchpad_elf_load_basic(lp, vmo);
        check("launchpad_elf_load_basic", status);
    } else {
        status = launchpad_elf_load(lp, vmo);
        check("launchpad_elf_load", status);
    }

    if (send_loader_message) {
        bool already_sending = launchpad_send_loader_message(lp, true);
        if (!already_sending) {
            mx_handle_t loader_svc = mxio_loader_service(NULL, NULL);
            check("mxio_loader_service", loader_svc);
            mx_handle_t old = launchpad_use_loader_service(lp, loader_svc);
            check("launchpad_use_loader_service", old);
            if (old != MX_HANDLE_INVALID) {
                fprintf(stderr, "launchpad_use_loader_service returned %#x\n",
                        old);
                return 2;
            }
        }
    }

    if (pass_loader_handle) {
        mx_handle_t loader_svc = mxio_loader_service(NULL, NULL);
        check("mxio_loader_service", loader_svc);
        status = launchpad_add_handle(lp, loader_svc, PA_SVC_LOADER);
        check("launchpad_add_handle", status);
    }

    // Note that if both -v and -V were passed, we'll add two separate
    // MX_HND_TYPE_EXEC_VMO handles to the startup message, which is
    // unlikely to be useful.  But this program is mainly to test the
    // library, so it makes all the library calls the user asks for.
    if (exec_vmo_file != NULL) {
        mx_handle_t exec_vmo = launchpad_vmo_from_file(exec_vmo_file);
        if (exec_vmo == ERR_IO) {
            perror(exec_vmo_file);
            return 2;
        }
        check("launchpad_vmo_from_file", exec_vmo);
        status = launchpad_add_handle(lp, exec_vmo, PA_VMO_EXECUTABLE);
    }

    if (exec_vmo_fd != -1) {
        mx_handle_t exec_vmo = launchpad_vmo_from_fd(exec_vmo_fd);
        if (exec_vmo == ERR_IO) {
            perror("launchpad_vmo_from_fd");
            return 2;
        }
        check("launchpad_vmo_from_fd", exec_vmo);
        status = launchpad_add_handle(lp, exec_vmo, PA_VMO_EXECUTABLE);
    }

    if (stack_size != (size_t)-1) {
        size_t old_size = launchpad_set_stack_size(lp, stack_size);
        assert(old_size > 0);
        assert(old_size < (size_t)-1);
    }

    // This doesn't get ownership of the process handle.
    // We're just testing the invariant that it returns a valid handle.
    mx_handle_t proc = launchpad_get_process_handle(lp);
    check("launchpad_get_process_handle", proc);

    // This gives us ownership of the process handle.
    proc = launchpad_start(lp);
    check("launchpad_start", proc);

    // The launchpad is done.  Clean it up.
    launchpad_destroy(lp);

    status = mx_object_wait_one(proc, MX_PROCESS_TERMINATED, MX_TIME_INFINITE, NULL);
    check("mx_object_wait_one", status);

    mx_info_process_t info;
    status = mx_object_get_info(proc, MX_INFO_PROCESS, &info, sizeof(info), NULL, NULL);
    check("mx_object_get_info", status);

    if (job)
        mx_handle_close(job);

    printf("Process finished with return code %d\n", info.return_code);
    return info.return_code;
}
