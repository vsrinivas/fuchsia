// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <limits.h>

#include <magenta/listnode.h>

#include <magenta/types.h>
#include <magenta/device/input.h>

#include <mxio/io.h>
#include <mxio/watcher.h>

#define DEV_INPUT "/dev/class/input"

static bool verbose = false;
#define xprintf(fmt...) do { if (verbose) printf(fmt); } while (0)

void usage(void) {
    printf("usage: hid [-v] <command> [<args>]\n\n");
    printf("  commands:\n");
    printf("    read [<devpath> [num reads]]\n");
    printf("    get <devpath> <in|out|feature> <id>\n");
    printf("    set <devpath> <in|out|feature> <id> [0xXX *]\n");
    printf("  all values are parsed as hexadecimal integers\n");
}

typedef struct input_args {
    int fd;
    char name[128];
    unsigned long int num_reads;
} input_args_t;

static thrd_t input_poll_thread;

static mtx_t print_lock = MTX_INIT;
#define lprintf(fmt...) \
    do { \
        mtx_lock(&print_lock); \
        printf(fmt); \
        mtx_unlock(&print_lock); \
    } while (0)


static void print_hex(uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", buf[i]);
        if (i % 16 == 15) printf("\n");
    }
    printf("\n");
}

static int get_hid_protocol(int fd, const char* name) {
    int proto;
    ssize_t rc = ioctl_input_get_protocol(fd, &proto);
    if (rc < 0) {
        lprintf("hid: could not get protocol from %s (status=%zd)\n", name, rc);
    } else {
        lprintf("hid: %s proto=%d\n", name, proto);
    }
    return rc;
}

static int get_report_desc_len(int fd, const char* name, size_t* report_desc_len) {
    ssize_t rc = ioctl_input_get_report_desc_size(fd, report_desc_len);
    if (rc < 0) {
        lprintf("hid: could not get report descriptor length from %s (status=%zd)\n", name, rc);
    } else {
        lprintf("hid: %s report descriptor len=%zu\n", name, *report_desc_len);
    }
    return rc;
}

static int get_report_desc(int fd, const char* name, size_t report_desc_len) {
    uint8_t* buf = malloc(report_desc_len);
    if (!buf) {
        lprintf("hid: out of memory\n");
        return ERR_NO_MEMORY;
    }
    ssize_t rc = ioctl_input_get_report_desc(fd, buf, report_desc_len);
    if (rc < 0) {
        lprintf("hid: could not get report descriptor from %s (status=%zd)\n", name, rc);
        free(buf);
        return rc;
    }
    mtx_lock(&print_lock);
    printf("hid: %s report descriptor:\n", name);
    print_hex(buf, report_desc_len);
    mtx_unlock(&print_lock);
    free(buf);
    return rc;
}

static int get_num_reports(int fd, const char* name, size_t* num_reports) {
    ssize_t rc = ioctl_input_get_num_reports(fd, num_reports);
    if (rc < 0) {
        lprintf("hid: could not get number of reports from %s (status=%zd)\n", name, rc);
    } else {
        lprintf("hid: %s num reports: %zu\n", name, *num_reports);
    }
    return rc;
}

static int get_report_ids(int fd, const char* name, size_t num_reports) {
    size_t out_len = num_reports * sizeof(input_report_id_t);
    input_report_id_t* ids = malloc(out_len);
    if (!ids) return ERR_NO_MEMORY;

    ssize_t rc = ioctl_input_get_report_ids(fd, ids, out_len);
    if (rc < 0) {
        lprintf("hid: could not get report ids from %s (status=%zd)\n", name, rc);
        free(ids);
        return rc;
    }
    mtx_lock(&print_lock);
    printf("hid: %s report ids: [", name);
    const char *s = "";
    for (size_t i = 0; i < num_reports; i++) {
        input_get_report_size_t arg;
        arg.id = ids[i];
        arg.type = INPUT_REPORT_INPUT;  // TODO: get all types
        input_report_size_t size;
        ssize_t size_rc = ioctl_input_get_report_size(fd, &arg, &size);
        if (size_rc < 0) {
            printf("hid: could not get report id size from %s (status=%zd)\n", name, size_rc);
            continue;
        }
        if (i > 0) s = " ";
        printf("%s%d(%d bytes)", s, ids[i], size);
    }
    printf("]\n");
    mtx_unlock(&print_lock);
    free(ids);
    return rc;
}

static int get_max_report_len(int fd, const char* name, input_report_size_t* max_report_len) {
    input_report_size_t tmp;
    if (max_report_len == NULL) {
        max_report_len = &tmp;
    }
    ssize_t rc = ioctl_input_get_max_reportsize(fd, max_report_len);
    if (rc < 0) {
        lprintf("hid: could not get max report size from %s (status=%zd)\n", name, rc);
    } else {
        lprintf("hid: %s maxreport=%u\n", name, *max_report_len);
    }
    return rc;
}

#define try(fn) \
    do { \
        int rc = fn; \
        if (rc < 0) return rc; \
    } while (0)

static int hid_status(int fd, const char* name, input_report_size_t* max_report_len) {
    size_t report_desc_len;
    size_t num_reports;

    try(get_hid_protocol(fd, name));
    try(get_report_desc_len(fd, name, &report_desc_len));
    try(get_report_desc(fd, name, report_desc_len));
    try(get_num_reports(fd, name, &num_reports));
    try(get_report_ids(fd, name, num_reports));
    try(get_max_report_len(fd, name, max_report_len));

    return NO_ERROR;
}

static int hid_input_thread(void* arg) {
    input_args_t* args = (input_args_t*)arg;
    lprintf("hid: input thread started for %s\n", args->name);

    input_report_size_t max_report_len = 0;
    try(hid_status(args->fd, args->name, &max_report_len));

    uint8_t* report = calloc(1, max_report_len);
    if (!report) return ERR_NO_MEMORY;

    for (uint32_t i = 0; i < args->num_reads; i++) {
        mxio_wait_fd(args->fd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
        int r = read(args->fd, report, max_report_len);
        mtx_lock(&print_lock);
        printf("read returned %d\n", r);
        if (r < 0) {
            mtx_unlock(&print_lock);
            break;
        }
        printf("hid: input from %s\n", args->name);
        print_hex(report, r);
        mtx_unlock(&print_lock);
    }
    free(report);
    lprintf("hid: closing %s\n", args->name);
    close(args->fd);
    free(args);
    return NO_ERROR;
}

static mx_status_t hid_input_device_added(int dirfd, const char* fn, void* cookie) {
    int fd = openat(dirfd, fn, O_RDONLY);
    if (fd < 0) {
        return NO_ERROR;
    }

    input_args_t* args = malloc(sizeof(*args));
    args->fd = fd;
    // TODO: support setting num_reads across all devices. requires a way to
    // signal shutdown to all input threads.
    args->num_reads = ULONG_MAX;
    thrd_t t;
    snprintf(args->name, sizeof(args->name), "hid-input-%s", fn);
    int ret = thrd_create_with_name(&t, hid_input_thread, (void*)args, args->name);
    if (ret != thrd_success) {
        printf("hid: input thread %s did not start (error=%d)\n", args->name, ret);
        close(fd);
    }
    thrd_detach(t);
    return NO_ERROR;
}

static int hid_input_devices_poll_thread(void* arg) {
    int dirfd = open(DEV_INPUT, O_DIRECTORY|O_RDONLY);
    if (dirfd < 0) {
        printf("hid: error opening %s\n", DEV_INPUT);
        return ERR_INTERNAL;
    }
    mxio_watch_directory(dirfd, hid_input_device_added, NULL);
    close(dirfd);
    return -1;
}

int read_reports(int argc, const char** argv) {
    argc--;
    argv++;
    if (argc < 1) {
        usage();
        return 0;
    }

    int fd = open(argv[0], O_RDWR);
    if (fd < 0) {
        printf("could not open %s: %d\n", argv[0], errno);
        return -1;
    }
    input_args_t* args = calloc(1, sizeof(*args));
    args->fd = fd;
    args->num_reads = ULONG_MAX;
    if (argc > 1) {
        errno = 0;
        args->num_reads = strtoul(argv[1], NULL, 10);
        if (errno) {
            usage();
            free(args);
            return 0;
        }
    }
    strlcpy(args->name, argv[0], sizeof(args->name));
    thrd_t t;
    int ret = thrd_create_with_name(&t, hid_input_thread, (void*)args, args->name);
    if (ret != thrd_success) {
        printf("hid: input thread %s did not start (error=%d)\n", args->name, ret);
        close(fd);
    }
    thrd_join(t, NULL);
    return 0;
}

int readall_reports(int argc, const char** argv) {
    int ret = thrd_create_with_name(&input_poll_thread, hid_input_devices_poll_thread, NULL, "hid-inputdev-poll");
    if (ret != thrd_success) {
        return ret;
    }

    thrd_join(input_poll_thread, NULL);
    return 0;
}

int get_report(int argc, const char** argv) {
    argc--;
    argv++;
    if (argc < 3) {
        usage();
        return 0;
    }

    int fd = open(argv[0], O_RDWR);
    if (fd < 0) {
        printf("could not open %s: %d\n", argv[0], errno);
        return -1;
    }

    input_get_report_size_t size_arg;
    size_arg.id = strtoul(argv[2], NULL, 16);
    size_arg.type = strtoul(argv[1], NULL, 16);
    xprintf("hid: getting report size for id=%u type=%u\n", size_arg.id, size_arg.type);

    input_report_size_t size;
    ssize_t rc = ioctl_input_get_report_size(fd, &size_arg, &size);
    if (rc < 0) {
        printf("hid: could not get report id size from %s (status=%zd)\n", argv[0], rc);
        return rc;
    }
    xprintf("hid: report size=%u\n", size);

    input_get_report_t rpt_arg;
    rpt_arg.id = size_arg.id;
    rpt_arg.type = size_arg.type;
    uint8_t* buf = malloc(size);
    rc = ioctl_input_get_report(fd, &rpt_arg, buf, size);
    if (rc < 0) {
        printf("hid: could not get report: %zd\n", rc);
    } else {
        printf("hid: report\n");
        print_hex(buf, size);
    }
    free(buf);
    return rc;
}

int set_report(int argc, const char** argv) {
    argc--;
    argv++;
    if (argc < 4) {
        usage();
        return 0;
    }

    int fd = open(argv[0], O_RDWR);
    if (fd < 0) {
        printf("could not open %s: %d\n", argv[0], errno);
        return -1;
    }

    input_get_report_size_t size_arg;
    size_arg.id = strtoul(argv[2], NULL, 16);
    size_arg.type = strtoul(argv[1], NULL, 16);
    xprintf("hid: getting report size for id=%u type=%u\n", size_arg.id, size_arg.type);

    input_report_size_t size;
    ssize_t rc = ioctl_input_get_report_size(fd, &size_arg, &size);
    if (rc < 0) {
        printf("hid: could not get report id size from %s (status=%zd)\n", argv[0], rc);
        return rc;
    }
    xprintf("hid: report size=%u\n", size);
    if (argc - 3 < size) {
        printf("not enough data for set\n");
        return -1;
    } else if (argc - 3 > size) {
        printf("ignoring extra data\n");
    }

    size_t in_len = sizeof(input_set_report_t) + size;
    input_set_report_t* arg = malloc(in_len);
    arg->id = size_arg.id;
    arg->type = size_arg.type;
    for (int i = 0; i < size; i++) {
        arg->data[i] = strtoul(argv[i+3], NULL, 16);
    }
    rc = ioctl_input_set_report(fd, arg, in_len);
    if (rc < 0) {
        printf("hid: could not set report: %zd\n", rc);
    } else {
        printf("hid: success\n");
    }
    free(arg);
    return rc;
}

int main(int argc, const char** argv) {
    if (argc < 2) {
        usage();
        return 0;
    }
    argc--;
    argv++;
    if (!strcmp("-v", argv[0])) {
        verbose = true;
        argc--;
        argv++;
    }
    if (!strcmp("read", argv[0])) {
        if (argc > 1) {
            return read_reports(argc, argv);
        } else {
            return readall_reports(argc, argv);
        }
    }
    if (!strcmp("get", argv[0])) return get_report(argc, argv);
    if (!strcmp("set", argv[0])) return set_report(argc, argv);
    usage();
    return 0;
}
