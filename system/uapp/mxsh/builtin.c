// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mxsh.h"

#include <magenta/syscalls.h>
#include <mxio/util.h>
#include <mxio/vfs.h>
#include <mxu/hexdump.h>
#include <system/listnode.h>

static int mxc_dump(int argc, char** argv) {
    int fd;
    ssize_t len;
    off_t off;
    char buf[4096];

    if (argc != 2) {
        printf("usage: dump <filename>\n");
        return -1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("error: cannot open '%s'\n", argv[1]);
        return -1;
    }
    off = 0;
    for (;;) {
        len = read(fd, buf, sizeof(buf));
        if (len <= 0) {
            if (len)
                printf("error: io\n");
            break;
        }
        mxu_hexdump8_ex(buf, len, off);
        off += len;
    }
    close(fd);
    return len;
}

static int mxc_echo(int argc, char** argv) {
    argc--;
    argv++;
    while (argc > 0) {
        write(1, argv[0], strlen(argv[0]));
        argc--;
        argv++;
        if (argc)
            write(1, " ", 1);
    }
    write(1, "\n", 1);
    return 0;
}

static int mxc_cd(int argc, char** argv) {
    if (argc < 2) {
        return 0;
    }
    if (chdir(argv[1])) {
        printf("error: cannot change directory to '%s'\n", argv[1]);
        return -1;
    }
    return 0;
}

static const char* modestr(uint32_t mode) {
    switch (mode & S_IFMT) {
    case S_IFREG:
        return "-";
    case S_IFCHR:
        return "c";
    case S_IFBLK:
        return "b";
    case S_IFDIR:
        return "d";
    default:
        return "?";
    }
}


static int mxc_ls(int argc, char** argv) {
    const char* dirn;
    struct stat s;
    char tmp[2048];
    size_t dirln;
    struct dirent* de;
    DIR* dir;

    if ((argc > 1) && !strcmp(argv[1], "-l")) {
        argc--;
        argv++;
    }
    if (argc < 2) {
        dirn = ".";
    } else {
        dirn = argv[1];
    }
    dirln = strlen(dirn);

    if (argc > 2) {
        printf("usage: ls [ <directory> ]\n");
        return -1;
    }
    if ((dir = opendir(dirn)) == NULL) {
        printf("error: cannot open '%s'\n", dirn);
        return -1;
    }
    while((de = readdir(dir)) != NULL) {
        memset(&s, 0, sizeof(struct stat));
        if ((strlen(de->d_name) + dirln + 2) <= sizeof(tmp)) {
            snprintf(tmp, sizeof(tmp), "%s/%s", dirn, de->d_name);
            stat(tmp, &s);
        }
        printf("%s %8llu %s\n", modestr(s.st_mode), s.st_size, de->d_name);
    }
    closedir(dir);
    return 0;
}

static int mxc_list(int argc, char** argv) {
    char line[1024];
    FILE* fp;
    int num = 1;

    if (argc != 2) {
        printf("usage: list <filename>\n");
        return -1;
    }

    fp = fopen(argv[1], "r");
    if (fp == NULL) {
        printf("error: cannot open '%s'\n", argv[1]);
        return -1;
    }
    while (fgets(line, 1024, fp) != NULL) {
        printf("%5d | %s", num, line);
        num++;
    }
    fclose(fp);
    return 0;
}

static int mxc_cp(int argc, char** argv) {
    char data[4096];
    int fdi = -1, fdo = -1;
    int r, wr;
    int count = 0;
    if (argc != 3) {
        printf("usage: cp <srcfile> <dstfile>\n");
        return -1;
    }
    if ((fdi = open(argv[1], O_RDONLY)) < 0) {
        printf("error: cannot open '%s'\n", argv[1]);
        return -1;
    }
    if ((fdo = open(argv[2], O_WRONLY | O_CREAT)) < 0) {
        printf("error: cannot open '%s'\n", argv[2]);
        r = fdo;
        goto done;
    }
    for (;;) {
        if ((r = read(fdi, data, sizeof(data))) < 0) {
            printf("error: failed reading from '%s'\n", argv[1]);
            break;
        }
        if (r == 0) {
            break;
        }
        if ((wr = write(fdo, data, r)) != r) {
            printf("error: failed writing to '%s'\n", argv[2]);
            r = wr;
            break;
        }
        count += r;
    }
    printf("[copied %d bytes]\n", count);
done:
    close(fdi);
    close(fdo);
    return r;
}

static int mxc_rm(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: rm <filename>\n");
        return -1;
    }
    while (argc > 1) {
        argc--;
        argv++;
        if (unlink(argv[0])) {
            printf("error: failed to delete '%s'\n", argv[0]);
        }
    }
    return 0;
}

typedef struct failure {
    list_node_t node;
    int cause;
    int rc;
    char name[0];
} failure_t;

static void mxc_fail_test(list_node_t* failures, const char* name, int cause, int rc) {
    size_t name_len = strlen(name) + 1;
    failure_t* failure = malloc(sizeof(failure_t) + name_len);
    failure->cause = cause;
    failure->rc = rc;
    memcpy(failure->name, name, name_len);
    list_add_tail(failures, &failure->node);
}

#define TESTNAME_SUFFIX "-test"
enum {
    FAILED_TO_LAUNCH,
    FAILED_TO_WAIT,
    FAILED_TO_RETURN_CODE,
    FAILED_NONZERO_RETURN_CODE,
};
static int mxc_runtests(int argc, char** argv) {
    list_node_t failures = LIST_INITIAL_VALUE(failures);

    int total_count = 0;
    int failed_count = 0;

    const char* dirn = "/boot/bin";
    DIR* dir = opendir(dirn);
    if (dir == NULL) {
        printf("error: cannot open '%s'\n", dirn);
        return -1;
    }
    size_t test_suffix_len = sizeof(TESTNAME_SUFFIX) - 1;

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len < test_suffix_len) {
            continue;
        }
        char* suffix = de->d_name + len - test_suffix_len;
        if (strncmp(suffix, TESTNAME_SUFFIX, test_suffix_len)) {
            continue;
        }
        total_count++;

        printf(
            "\n------------------------------------------------\n"
            "RUNNING TEST: %s\n\n",
            de->d_name);
        char name[4096];
        snprintf(name, sizeof(name), "/boot/bin/%s", de->d_name);
        char* argv[] = {name};
        mx_handle_t handle = mxio_start_process(name, 1, argv);
        if (handle < 0) {
            printf("FAILURE: Failed to launch %s: %d\n", de->d_name, handle);
            mxc_fail_test(&failures, de->d_name, FAILED_TO_LAUNCH, 0);
            failed_count++;
            continue;
        }

        mx_status_t status = _magenta_handle_wait_one(handle, MX_SIGNAL_SIGNALED,
                                                      MX_TIME_INFINITE, 0, 0);
        if (status != NO_ERROR) {
            printf("FAILURE: Failed to wait for process exiting %s: %d\n", de->d_name, status);
            mxc_fail_test(&failures, de->d_name, FAILED_TO_WAIT, 0);
            failed_count++;
            continue;
        }

        // read the return code
        mx_process_info_t proc_info;
        mx_ssize_t info_status = _magenta_handle_get_info(handle, MX_INFO_PROCESS, &proc_info, sizeof(proc_info));
        _magenta_handle_close(handle);

        if (info_status != sizeof(proc_info)) {
            printf("FAILURE: Failed to get process return code %s: %ld\n", de->d_name, info_status);
            mxc_fail_test(&failures, de->d_name, FAILED_TO_RETURN_CODE, 0);
            failed_count++;
            continue;
        }

        if (proc_info.return_code == 0) {
            printf("PASSED: %s passed\n", de->d_name);
        } else {
            printf("FAILED: %s exited with nonzero status: %d\n", de->d_name, proc_info.return_code);
            mxc_fail_test(&failures, de->d_name, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
            failed_count++;
        }
    }

    closedir(dir);

    printf("\nSUMMARY: Ran %d tests: %d failed\n", total_count, failed_count);

    if (failed_count) {
        printf("\nThe following tests failed:\n");
        failure_t* failure = NULL;
        failure_t* temp = NULL;
        list_for_every_entry_safe (&failures, failure, temp, failure_t, node) {
            switch (failure->cause) {
            case FAILED_TO_LAUNCH:
                printf("%s: failed to launch\n", failure->name);
                break;
            case FAILED_TO_WAIT:
                printf("%s: failed to wait\n", failure->name);
                break;
            case FAILED_TO_RETURN_CODE:
                printf("%s: failed to return exit code\n", failure->name);
                break;
            case FAILED_NONZERO_RETURN_CODE:
                printf("%s: returned nonzero: %d\n", failure->name, failure->rc);
                break;
            }
            free(failure);
        }
    }

    return 0;
}

static int mxc_dm(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: dm <command>\n");
        return -1;
    }
    int fd = open("/dev/dmctl", O_RDWR);
    if (fd >= 0) {
        int r = write(fd, argv[1], strlen(argv[1]));
        if (r < 0) {
            printf("error: cannot write dmctl: %d\n", r);
        }
        close(fd);
        return r;
    } else {
        printf("error: cannot open dmctl: %d\n", fd);
        return fd;
    }
}

static int mxc_help(int argc, char** argv);

builtin_t builtins[] = {
    {"cd", mxc_cd, "change directory"},
    {"cp", mxc_cp, "copy a file"},
    {"dump", mxc_dump, "display a file in hexadecimal"},
    {"echo", mxc_echo, "print its arguments"},
    {"help", mxc_help, "list built-in shell commands"},
    {"dm", mxc_dm, "send command to device manager"},
    {"list", mxc_list, "display a text file with line numbers"},
    {"ls", mxc_ls, "list directory contents"},
    {"rm", mxc_rm, "delete a file"},
    {"runtests", mxc_runtests, "run all test programs"},
    {NULL, NULL, NULL},
};

static int mxc_help(int argc, char** argv) {
    builtin_t* b;
    int n = 8;
    for (b = builtins; b->name != NULL; b++) {
        int len = strlen(b->name);
        if (len > n)
            n = len;
    }
    for (b = builtins; b->name != NULL; b++) {
        printf("%-*s  %s\n", n, b->name, b->desc);
    }
    printf("%-*s %s\n", n, "<program>", "run <program>");
    printf("%-*s  %s\n\n", n, "`command", "send command to kernel console");
    return 0;
}
