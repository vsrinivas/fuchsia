// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <magenta/assert.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include <mxio/debug.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <magenta/listnode.h>

#include <linenoise/linenoise.h>

#include "mxsh.h"

static bool interactive = false;
static mx_handle_t job_handle;
static mx_handle_t app_env_handle;

static void cputs(const char* s, size_t len) {
    write(1, s, len);
}

static void settitle(const char* title) {
    if (!interactive) {
        return;
    }
    char str[16];
    int n = snprintf(str, sizeof(str) - 1, "\033]2;%s", title);
    if (n < 0) {
        return; // error
    } else if ((size_t)n >= sizeof(str) - 1) {
        n = sizeof(str) - 2; // truncated
    }
    str[n] = '\007';
    str[n+1] = '\0';
    cputs(str, n + 1);
}

static const char* system_paths[] = {
    "/system/bin",
    "/boot/bin",
    NULL,
};

typedef struct {
    // An index into the tokenized string which points at the first
    // character of the last token (ie space separated component) of
    // the line.
    size_t start;
    // Whether there are multiple non-enviroment components of the
    // line to tokenize. For example:
    //     foo          # found_command = false;
    //     foo bar      # found_command = true;
    //     FOO=BAR quux # found_command = false;
    bool found_command;
    // Whether the end of the line is in a space-free string of the
    // form 'FOO=BAR', which is the syntax to set an environment
    // variable.
    bool in_env;
} token_t;

static token_t tokenize(const char* line, size_t line_length) {
    token_t token = {
        .start = 0u,
        .found_command = false,
        .in_env = false,
    };
    bool in_token = false;

    for (size_t i = 0; i < line_length; i++) {
        if (line[i] == ' ') {
            token.start = i + 1;

            if (in_token && !token.in_env) {
                token.found_command = true;
            }

            in_token = false;
            token.in_env = false;
            continue;
        }

        in_token = true;
        token.in_env = token.in_env || line[i] == '=';
    }

    return token;
}

typedef struct {
    const char* line_prefix;
    const char* line_separator;
    const char* file_prefix;
} completion_state_t;

// Generate file name completions. |dir| is the directory to for
// matching filenames. File names must match |state->file_prefix| in
// order to be entered into |completions|. |state->line_prefix| and
// |state->line_separator| begin the line before the file completion.
static void complete_at_dir(DIR* dir, completion_state_t* state,
                            linenoiseCompletions* completions) {
    DEBUG_ASSERT(strchr(state->file_prefix, '/') == NULL);
    size_t file_prefix_len = strlen(state->file_prefix);

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(state->file_prefix, de->d_name, file_prefix_len)) {
            continue;
        }
        if (!strcmp(de->d_name, ".")) {
            continue;
        }
        if (!strcmp(de->d_name, "..")) {
            continue;
        }

        char completion[LINE_MAX];
        strncpy(completion, state->line_prefix, sizeof(completion));
        completion[sizeof(completion) - 1] = '\0';
        strncat(completion, state->line_separator, sizeof(completion));
        strncat(completion, de->d_name, sizeof(completion));

        linenoiseAddCompletion(completions, completion);
    }
}

static void tab_complete(const char* line, linenoiseCompletions* completions) {
    size_t input_line_length = strlen(line);

    token_t token = tokenize(line, input_line_length);

    if (token.in_env) {
        // We can't tab complete environment variables.
        return;
    }

    char buf[LINE_MAX];
    size_t token_length = input_line_length - token.start;
    if (token_length >= sizeof(buf)) {
        return;
    }
    strncpy(buf, line, sizeof(buf));
    char* partial_path = buf + token.start;

    // The following variables are set by the following block of code
    // in each of three different cases:
    //
    // 1. There is no slash in the last token, and we are giving an
    //    argument to a command. An example:
    //        foo bar ba
    //    We are searching the current directory (".") for files
    //    matching the prefix "ba", to join with a space to the line
    //    prefix "foo bar".
    //
    // 2. There is no slash in the only token. An example:
    //        fo
    //    We are searching the system paths (currently "/system/bin"
    //    and "/boot/bin") for files matching the prefix "fo". There
    //    is no line prefix or separator in this case.
    //
    // 3. There is a slash in the last token. An example:
    //        foo bar baz/quu
    //    In this case, we are searching the directory specified by
    //    the token (up until the final '/', so "baz" in this case)
    //    for files with the prefix "quu", to join with a slash to the
    //    line prefix "foo bar baz".
    completion_state_t completion_state;
    const char** paths;

    // |paths| for cases 1 and 3 respectively.
    const char* local_paths[] = { ".", NULL };
    const char* partial_paths[] = { partial_path, NULL };

    char* file_prefix = strrchr(partial_path, '/');
    if (file_prefix == NULL) {
        file_prefix = partial_path;
        if (token.found_command) {
            // Case 1.
            // Because we are in a command, partial_path[-1] is a
            // space we want to zero out.
            DEBUG_ASSERT(token.start > 0);
            DEBUG_ASSERT(partial_path[-1] == ' ');
            partial_path[-1] = '\0';

            completion_state.line_prefix = buf;
            completion_state.line_separator = " ";
            completion_state.file_prefix = file_prefix;
            paths = local_paths;
        } else {
            // Case 2.
            completion_state.line_prefix = "";
            completion_state.line_separator = "";
            completion_state.file_prefix = file_prefix;
            paths = system_paths;
        }
    } else {
        // Case 3.
        // Because we are in a multiple component file path,
        // *file_prefix is a '/' we want to zero out.
        DEBUG_ASSERT(*file_prefix == '/');
        *file_prefix = '\0';

        completion_state.line_prefix = buf;
        completion_state.line_separator = "/";
        completion_state.file_prefix = file_prefix + 1;
        paths = partial_paths;
    }

    for (; *paths != NULL; paths++) {
        DIR* dir = opendir(*paths);
        if (dir == NULL) {
            continue;
        }
        complete_at_dir(dir, &completion_state, completions);
        closedir(dir);
    }
}

static int split(char* line, char* argv[], int max) {
    int n = 0;
    while (max > 0) {
        while (isspace(*line))
            line++;
        if (*line == 0)
            break;
        argv[n++] = line;
        max--;
        line++;
        while (*line && (!isspace(*line)))
            line++;
        if (*line == 0)
            break;
        *line++ = 0;
    }
    return n;
}

static void joinproc(mx_handle_t p) {
    mx_status_t r;

    r = mx_handle_wait_one(p, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL);
    if (r != NO_ERROR) {
        fprintf(stderr, "[process(%x): wait failed? %d]\n", p, r);
        return;
    }

    // read the return code
    mx_info_process_t proc_info;
    if ((r = mx_object_get_info(p, MX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL)) < 0) {
        fprintf(stderr, "[process(%x): object_get_info failed? %d\n", p, r);
    } else {
        fprintf(stderr, "[process(%x): status: %d]\n", p, proc_info.return_code);
    }

    settitle("mxsh");
    mx_handle_close(p);
}

static void* joiner(void* arg) {
    joinproc((uintptr_t)arg);
    return NULL;
}

static mx_status_t lp_setup(launchpad_t** lp_out, mx_handle_t job,
                     int argc, const char* const* argv,
                     const char* const* envp) {
    launchpad_t* lp;

    mx_handle_t job_copy = MX_HANDLE_INVALID;
    mx_status_t status = mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_copy);
    if (status < 0)
        return status;

    if ((status = launchpad_create(job_copy, argv[0], &lp)) < 0) {
        return status;
    }
    if ((status = launchpad_arguments(lp, argc, argv)) < 0) {
        goto fail;
    }
    if ((status = launchpad_environ(lp, envp)) < 0) {
        goto fail;
    }
    if ((status = launchpad_add_vdso_vmo(lp)) < 0) {
        goto fail;
    }
    if ((status = launchpad_clone_mxio_root(lp)) < 0) {
        goto fail;
    }
    *lp_out = lp;
    return NO_ERROR;

fail:
    launchpad_destroy(lp);
    return status;
}

typedef struct {
    uint32_t header_size;
    uint32_t header_version;
    uint32_t message_ordinal;
    uint32_t message_flags;
    uint32_t message_size;
    uint32_t message_version;
    uint32_t handle;
    uint32_t padding;
} dup_message_t;

static mx_status_t dup_app_env(mx_handle_t* dup_handle) {
    dup_message_t dm;
    dm.header_size = 16;
    dm.header_version = 0;
    dm.message_ordinal = 0; // must match application_environment.fidl
    dm.message_flags = 0;
    dm.message_size = 16;
    dm.message_version = 0;
    dm.handle = 0;
    dm.padding = 0;

    mx_handle_t request_handle;
    mx_status_t status;
    if ((status = mx_channel_create(0, &request_handle, dup_handle)))
        return status;

    if ((status = mx_channel_write(app_env_handle, 0, &dm, sizeof(dm), &request_handle, 1))) {
        mx_handle_close(request_handle);
        mx_handle_close(*dup_handle);
        *dup_handle = MX_HANDLE_INVALID;
    }
    return status;
}

static mx_status_t command(int argc, char** argv, bool runbg) {
    char tmp[LINE_MAX + 32];
    launchpad_t* lp;
    mx_status_t status = NO_ERROR;
    int i;

    // Leading FOO=BAR become environment strings prepended to the
    // inherited environ, just like in a real Bourne shell.
    const char** envp = (const char**)environ;
    for (i = 0; i < argc; ++i) {
        if (strchr(argv[i], '=') == NULL)
            break;
    }
    if (i > 0) {
        size_t envc = 1;
        for (char** ep = environ; *ep != NULL; ++ep)
            ++envc;
        envp = malloc((i + envc) * sizeof(*envp));
        if (envp == NULL) {
            puts("out of memory for environment strings!");
            return ERR_NO_MEMORY;
        }
        memcpy(mempcpy(envp, argv, i * sizeof(*envp)),
               environ, envc * sizeof(*envp));
        argc -= i;
        argv += i;
    }

    // Simplistic stdout redirection support
    int stdout_fd = -1;
    if ((argc > 0) && (argv[argc - 1][0] == '>')) {
        const char* fn = argv[argc - 1] + 1;
        while (isspace(*fn)) {
            fn++;
        }
        unlink(fn);
        if ((stdout_fd = open(fn, O_WRONLY | O_CREAT)) < 0) {
            fprintf(stderr, "cannot open '%s' for writing\n", fn);
            goto done_no_lp;
        }
        argc--;
    }

    if (argc == 0) {
        goto done_no_lp;
    }

    for (i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(builtins[i].name, argv[0]))
            continue;
        if (stdout_fd >= 0) {
            fprintf(stderr, "redirection not supported for builtin functions\n");
            status = ERR_NOT_SUPPORTED;
            goto done_no_lp;
        }
        settitle(argv[0]);
        builtins[i].func(argc, argv);
        fflush(stdout);
        settitle("mxsh");
        goto done_no_lp;
    }

    int fd;
    if (argv[0][0] != '/' && argv[0][0] != '.') {
        for (const char** path = system_paths; *path != NULL; path++) {
            snprintf(tmp, sizeof(tmp), "%s/%s", *path, argv[0]);
            if ((fd = open(tmp, O_RDONLY)) >= 0) {
                argv[0] = tmp;
                goto found;
            }
        }
        fprintf(stderr, "could not load binary '%s'\n", argv[0]);
        goto done_no_lp;
    }

    if ((fd = open(argv[0], O_RDONLY)) < 0) {
        fprintf(stderr, "could not open binary '%s'\n", argv[0]);
        goto done_no_lp;
    }

found:
    if ((status = lp_setup(&lp, job_handle, argc, (const char* const*) argv, envp)) < 0) {
        fprintf(stderr, "process setup failed (%d)\n", status);
        goto done_no_lp;
    }

    status = launchpad_elf_load(lp, launchpad_vmo_from_fd(fd));
    close(fd);
    if (status < 0) {
        fprintf(stderr, "could not load binary '%s' (%d)\n", argv[0], status);
        goto done;
    }

    if ((status = launchpad_load_vdso(lp, MX_HANDLE_INVALID)) < 0) {
        fprintf(stderr, "could not load vDSO after binary '%s' (%d)\n",
                argv[0], status);
        goto done;
    }

    status = launchpad_clone_mxio_cwd(lp);
    if(status != NO_ERROR) {
        fprintf(stderr, "could not copy cwd handle: (%d)\n", status);
        goto done;
    }

    // unclone-able files will end up as /dev/null in the launched process
    launchpad_clone_fd(lp, 0, 0);
    launchpad_clone_fd(lp, (stdout_fd >= 0) ? stdout_fd : 1, 1);
    launchpad_clone_fd(lp, 2, 2);

    if (app_env_handle) {
        mx_handle_t dup_handle;
        if ((status = dup_app_env(&dup_handle))) {
            fprintf(stderr, "could not dup application environment: (%d)\n", status);
        } else {
            launchpad_add_handle(lp, dup_handle,
                MX_HND_INFO(MX_HND_TYPE_APPLICATION_ENVIRONMENT, 0));
        }
    }

    mx_handle_t p;
    if ((p = launchpad_start(lp)) < 0) {
        fprintf(stderr, "process failed to start (%d)\n", p);
        status = p;
        goto done;
    }
    if (runbg) {
        // TODO: migrate to a unified waiter thread once we can wait
        //       on process exit
        pthread_t t;
        if (pthread_create(&t, NULL, joiner, (void*)((uintptr_t)p))) {
            mx_handle_close(p);
        }
    } else {
        char* bname = strrchr(argv[0], '/');
        if (!bname) {
            bname = argv[0];
        } else {
            bname += 1; // point to the first char after the last '/'
        }
        settitle(bname);
        joinproc(p);
    }
    status = NO_ERROR;
done:
    launchpad_destroy(lp);
done_no_lp:
    if (envp != (const char**)environ) {
        free(envp);
    }
    if (stdout_fd >= 0) {
        close(stdout_fd);
    }
    return status;
}

static void send_debug_command(const char* cmd) {
    const char* prefix = "kerneldebug ";
    char buf[256];
    size_t len = strlen(prefix) + strlen(cmd) + 1;
    if (len > sizeof(buf)) {
        return;
    }

    int fd = open("/dev/class/misc/dmctl", O_WRONLY);
    if (fd < 0) {
        return;
    }

    // If we detect someone trying to use the LK poweroff/reboot,
    // divert it to devmgr backed one instead.
    if (!strcmp(cmd, "poweroff") || !strcmp(cmd, "reboot")) {
        strcpy(buf, cmd);
        buf[strlen(cmd)] = '\0';
    } else {
        strcpy(buf, prefix);
        strncpy(buf + strlen(prefix), cmd, len - strlen(prefix));
        buf[len - 1] = '\0';
    }

    write(fd, buf, len);
    close(fd);
}

static void app_launch(const char* url) {
    int fd = open("/dev/class/misc/dmctl", O_WRONLY);
    if (fd >= 0) {
        int r = write(fd, url, strlen(url));
        if (r < 0) {
            fprintf(stderr, "error: cannot write dmctl: %d\n", r);
        }
        close(fd);
    } else {
        fprintf(stderr, "error: cannot open dmctl: %d\n", fd);
    }
}

static void execline(char* line) {
    bool runbg;
    char* argv[32];
    int argc;
    int len;

    if (line[0] == '`') {
        send_debug_command(line + 1);
        return;
    }
    len = strlen(line);

    // trim whitespace
    while ((len > 0) && (line[len - 1] <= ' ')) {
        len--;
        line[len] = 0;
    }

    if (!strncmp(line, "@", 1)) {
        app_launch(line);
        return;
    }

    // handle backgrounding
    if ((len > 0) && (line[len - 1] == '&')) {
        line[len - 1] = 0;
        runbg = true;
    } else {
        runbg = false;
    }

    // tokenize and execute
    argc = split(line, argv, 32);
    if (argc) {
        command(argc, argv, runbg);
    }
}

static void execscript(const char* fn) {
    char line[1024];
    FILE* fp;
    if ((fp = fopen(fn, "r")) == NULL) {
        printf("cannot open '%s'\n", fn);
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        execline(line);
    }
}

void greet(void) {
    const char* banner = "\033]2;mxsh\007\nMXCONSOLE...\n";
    cputs(banner, strlen(banner));

    char cmd[] = "motd";
    execline(cmd);
}

static void console(void) {
    linenoiseSetCompletionCallback(tab_complete);

    for (;;) {
        const char* const prompt = "> ";
        char* line = linenoise(prompt);
        if (line == NULL) {
            continue;
        }
        puts(line);
        linenoiseHistoryAdd(line);
        execline(line);
        linenoiseFree(line);
    }
}

int main(int argc, char** argv) {
    job_handle = mxio_get_startup_handle(MX_HND_INFO(MX_HND_TYPE_JOB, 0));
    if (job_handle <= 0)
        printf("<> no job %d\n", job_handle);

    app_env_handle = mxio_get_startup_handle(
        MX_HND_INFO(MX_HND_TYPE_APPLICATION_ENVIRONMENT, 0));

    if ((argc == 3) && (strcmp(argv[1], "-c") == 0)) {
        execline(argv[2]);
        return 0;
    }
    if (argc > 1) {
        for (int arg = 1; arg < argc; arg++) {
            execscript(argv[arg]);
        }
        return 0;
    }

    interactive = true;
    greet();
    console();
    return 0;
}
