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
#include <mxu/list.h>

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

int getdirents(int fd, void* ptr, size_t len);

static int mxc_ls(int argc, char** argv) {
    const char* dirn;
    struct stat s;
    char buf[4096];
    char tmp[2048];
    int fd, r, off;
    size_t dirln;

    if ((argc > 1) && !strcmp(argv[1], "-l")) {
        argc--;
        argv++;
    }
    if (argc < 2) {
        dirn = "/";
    } else {
        dirn = argv[1];
    }
    dirln = strlen(dirn);

    if (argc > 2) {
        printf("usage: ls [ <directory> ]\n");
        return -1;
    }
    fd = open(dirn, O_RDONLY);
    if (fd < 0) {
        printf("error: cannot open '%s'\n", dirn);
        return -1;
    }
    for (;;) {
        r = getdirents(fd, buf, sizeof(buf));
        if (r <= 0) {
            if (r)
                printf("error: reading dirents\n");
            break;
        }
        off = 0;
        while (off < r) {
            vdirent_t* de = (vdirent_t*)(buf + off);
            memset(&s, 0, sizeof(struct stat));
            if ((strlen(de->name) + dirln + 2) <= sizeof(tmp)) {
                snprintf(tmp, sizeof(tmp), "%s/%s", dirn, de->name);
                stat(tmp, &s);
            }
            printf("%s %8llu %s\n", modestr(s.st_mode), s.st_size, de->name);
            off += de->size;
        }
    }
    close(fd);
    return r;
}

#if WITH_LIBC_IO_HOOKS
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
#endif

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
    char buf[4096];
    list_node_t failures = LIST_INITIAL_VALUE(failures);
    int r, off;

    int total_count = 0;
    int failed_count = 0;

    const char* dirn = "/boot/bin";
    int fd = open(dirn, O_RDONLY);
    if (fd < 0) {
        printf("error: cannot open '%s'\n", dirn);
        return -1;
    }
    size_t test_suffix_len = sizeof(TESTNAME_SUFFIX) - 1;
    for (;;) {
        r = getdirents(fd, buf, sizeof(buf));
        if (r <= 0) {
            if (r)
                printf("error: reading dirents\n");
            break;
        }
        off = 0;
        while (off < r) {
            vdirent_t* de = (vdirent_t*)(buf + off);
            off += de->size;

            size_t len = strlen(de->name);
            if (len < test_suffix_len) {
                continue;
            }
            char* suffix = de->name + len - test_suffix_len;
            if (strncmp(suffix, TESTNAME_SUFFIX, test_suffix_len)) {
                continue;
            }
            total_count++;

            printf(
                "\n------------------------------------------------\n"
                "RUNNING TEST: %s\n\n",
                de->name);
            char name[4096];
            snprintf(name, sizeof(name), "/boot/bin/%s", de->name);
            char* argv[] = {name};
            mx_handle_t handle = mxio_start_process(1, argv);
            if (handle < 0) {
                printf("FAILURE: Failed to launch %s: %d\n", de->name, handle);
                mxc_fail_test(&failures, de->name, FAILED_TO_LAUNCH, 0);
                failed_count++;
                continue;
            }

            mx_status_t status = _magenta_handle_wait_one(handle, MX_SIGNAL_SIGNALED,
                                                          MX_TIME_INFINITE, 0, 0);
            if (status != NO_ERROR) {
                printf("FAILURE: Failed to wait for process exiting %s: %d\n", de->name, status);
                mxc_fail_test(&failures, de->name, FAILED_TO_WAIT, 0);
                failed_count++;
                continue;
            }

            // read the return code
            mx_process_info_t proc_info;
            status = _magenta_process_get_info(handle, &proc_info, sizeof(proc_info));
            _magenta_handle_close(handle);

            if (status != NO_ERROR) {
                printf("FAILURE: Failed to get process return code %s: %d\n", de->name, status);
                mxc_fail_test(&failures, de->name, FAILED_TO_RETURN_CODE, 0);
                failed_count++;
                continue;
            }

            if (proc_info.return_code == 0) {
                printf("PASSED: %s passed\n", de->name);
            } else {
                printf("FAILED: %s exited with nonzero status: %d\n", de->name, proc_info.return_code);
                mxc_fail_test(&failures, de->name, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
                failed_count++;
            }
        }
    }

    close(fd);

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
    {"cp", mxc_cp, "copy a file"},
    {"dump", mxc_dump, "display a file in hexadecimal"},
    {"echo", mxc_echo, "print its arguments"},
    {"help", mxc_help, "list built-in shell commands"},
    {"dm", mxc_dm, "send command to device manager"},
    {"list", mxc_list, "display a text file with line numbers"},
    {"ls", mxc_ls, "list directory contents"},
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
