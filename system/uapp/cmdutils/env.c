// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

extern char** environ;

static void usage(const char *exe_name)
{
    fprintf(stderr, "Usage: %s [options] [NAME=VALUE]... [command]\n", exe_name);
    fprintf(stderr, "Execute command in a modified environment or list environment\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -i      Set only the values provided\n");
    fprintf(stderr, "  --help  Print this message and exit\n");
}

static void dumpenv(const char **envp)
{
    while (*envp) {
        puts(*envp);
        envp++;
    }
}

static int env_var_count(char** envp) {
    int ndx = 0;
    while (envp[ndx])
        ndx++;
    return ndx;
}

int main(int argc, char* const argv[]) {

    int next_arg = 1;
    bool use_empty_env = false;

    if (argc > next_arg && argv[next_arg][0] == '-') {
        if (! strcmp(&argv[next_arg][1], "i")) {
            use_empty_env = true;
        } else if (! strcmp(&argv[next_arg][1], "-help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "%s: Unrecognized option '%s'\n", argv[0], argv[next_arg]);
            fprintf(stderr, "  use --help to get a list of options\n");
            return 125;
        }
        next_arg++;
    }

    int env_vars_ndx = next_arg;
    while (next_arg < argc && strchr(argv[next_arg], '='))
        next_arg++;

    int new_var_count = next_arg - env_vars_ndx;
    int old_var_count = use_empty_env ? 0 : env_var_count(environ);
    int total_var_count = new_var_count + old_var_count;

    // Construct our execution environment
    const char** envp = malloc(sizeof(const char*) * (total_var_count + 1));
    if (! envp) {
        fprintf(stderr, "Out of memory\n");
        return 124;
    }
    int env_ndx = 0;
    if (!use_empty_env) {
        while (environ[env_ndx]) {
            envp[env_ndx] = environ[env_ndx];
            env_ndx++;
        }
    }
    int arg_ndx;
    for(arg_ndx = env_vars_ndx; arg_ndx < next_arg; arg_ndx++) {
        envp[env_ndx] = argv[arg_ndx];
        env_ndx++;
    }
    envp[env_ndx] = NULL;

    // If no command is given, just dump the environment to stdout
    if (next_arg == argc) {
        dumpenv(envp);
        free(envp);
        return 0;
    }

    // Execute utility
    launchpad_t* lp;
    launchpad_create(MX_HANDLE_INVALID, argv[next_arg], &lp);
    mx_status_t status = launchpad_load_from_file(lp, argv[next_arg]);
    if (status < 0) {
        fprintf(stderr, "%s: Failed to load from '%s'\n", argv[0],
                argv[next_arg]);
        launchpad_destroy(lp);
        free(envp);
        return 127;
    }
    int num_args = (argc - next_arg);
    launchpad_set_args(lp, num_args, (const char* const*) &argv[next_arg]);
    launchpad_set_environ(lp, envp);
    launchpad_clone(lp, LP_CLONE_MXIO_NAMESPACE | LP_CLONE_MXIO_CWD |
                        LP_CLONE_MXIO_STDIO);
    mx_handle_t proc;
    const char* errmsg;
    status = launchpad_go(lp, &proc, &errmsg);
    free(envp);
    if (status < 0) {
        fprintf(stderr, "%s: Failed to launch: %s\n", argv[0], errmsg);
        return 126;
    }

    // Wait for utility to complete and return status
    status = mx_object_wait_one (proc, MX_TASK_TERMINATED,
                                 MX_TIME_INFINITE, NULL);
    if (status != MX_OK) {
        fprintf(stderr, "%s: Failed during object_wait_one\n", argv[0]);
        return 123;
    }
    mx_info_process_t proc_info;
    if (mx_object_get_info(proc, MX_INFO_PROCESS, &proc_info,
                           sizeof(proc_info), NULL, NULL) != MX_OK) {
        fprintf(stderr, "%s: Failed during object_get_info\n", argv[0]);
        return 122;
    }
    return proc_info.return_code & 0xff;
}

