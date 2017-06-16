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

#include <magenta/assert.h>
#include <magenta/listnode.h>
#include <magenta/threads.h>
#include <magenta/types.h>
#include <magenta/device/input.h>

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

static mx_status_t parse_uint_arg(const char* arg, uint32_t min, uint32_t max, uint32_t* out_val) {
    if ((arg == NULL) || (out_val == NULL)) {
        return MX_ERR_INVALID_ARGS;
    }

    bool is_hex = (arg[0] == '0') && (arg[1] == 'x');
    if (sscanf(arg, is_hex ? "%x" : "%u", out_val) != 1) {
        return MX_ERR_INVALID_ARGS;
    }

    if ((*out_val < min) || (*out_val > max)) {
        return MX_ERR_OUT_OF_RANGE;
    }

    return MX_OK;
}

static mx_status_t parse_input_report_type(const char* arg, input_report_type_t* out_type) {
    if ((arg == NULL) || (out_type == NULL)) {
        return MX_ERR_INVALID_ARGS;
    }

    static const struct {
        const char* name;
        input_report_type_t type;
    } LUT[] = {
        { .name = "in",      .type = INPUT_REPORT_INPUT },
        { .name = "out",     .type = INPUT_REPORT_OUTPUT },
        { .name = "feature", .type = INPUT_REPORT_FEATURE },
    };

    for (size_t i = 0; i < countof(LUT); ++i) {
        if (!strcasecmp(arg, LUT[i].name)) {
            *out_type = LUT[i].type;
            return MX_OK;
        }
    }

    return MX_ERR_INVALID_ARGS;
}

static mx_status_t parse_set_get_report_args(int argc,
                                             const char** argv,
                                             input_report_id_t* out_id,
                                             input_report_type_t* out_type) {
    if ((argc < 3) || (argv == NULL) || (out_id == NULL) || (out_type == NULL)) {
        return MX_ERR_INVALID_ARGS;
    }

    mx_status_t res;
    uint32_t tmp;
    res = parse_uint_arg(argv[2], 0, 255, &tmp);
    if (res != MX_OK) {
        return res;
    }

    *out_id = tmp;

    return parse_input_report_type(argv[1], out_type);
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
        return MX_ERR_NO_MEMORY;
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
    if (!ids) return MX_ERR_NO_MEMORY;

    ssize_t rc = ioctl_input_get_report_ids(fd, ids, out_len);
    if (rc < 0) {
        lprintf("hid: could not get report ids from %s (status=%zd)\n", name, rc);
        free(ids);
        return rc;
    }
    mtx_lock(&print_lock);
    printf("hid: %s report ids...\n", name);
    for (size_t i = 0; i < num_reports; i++) {
        static const struct {
            input_report_type_t type;
            const char* tag;
        } TYPES[] = {
            { .type = INPUT_REPORT_INPUT,   .tag = "Input" },
            { .type = INPUT_REPORT_OUTPUT,  .tag = "Output" },
            { .type = INPUT_REPORT_FEATURE, .tag = "Feature" },
        };

        bool found = false;
        for (size_t j = 0; j < countof(TYPES); ++j) {
            input_get_report_size_t arg = { .id = ids[i], .type = TYPES[j].type };
            input_report_size_t size;
            ssize_t size_rc;

            size_rc = ioctl_input_get_report_size(fd, &arg, &size);
            if (size_rc >= 0) {
                printf("  ID 0x%02x : TYPE %7s : SIZE %u bytes\n",
                        ids[i], TYPES[j].tag, size);
                found = true;
            }
        }

        if (!found) {
            printf("  hid: failed to find any report sizes for report id 0x%02x's (dev %s)\n",
                    ids[i], name);
        }
    }

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

    return MX_OK;
}

static int hid_input_thread(void* arg) {
    input_args_t* args = (input_args_t*)arg;
    lprintf("hid: input thread started for %s\n", args->name);

    input_report_size_t max_report_len = 0;
    try(hid_status(args->fd, args->name, &max_report_len));

    // Add 1 to the max report length to make room for a Report ID.
    max_report_len++;

    uint8_t* report = calloc(1, max_report_len);
    if (!report) return MX_ERR_NO_MEMORY;

    for (uint32_t i = 0; i < args->num_reads; i++) {
        int r = read(args->fd, report, max_report_len);
        mtx_lock(&print_lock);
        printf("read returned %d\n", r);
        if (r < 0) {
            printf("read errno=%d (%s)\n", errno, strerror(errno));
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
    return MX_OK;
}

static mx_status_t hid_input_device_added(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return MX_OK;
    }

    int fd = openat(dirfd, fn, O_RDONLY);
    if (fd < 0) {
        return MX_OK;
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
        return thrd_status_to_mx_status(ret);
    }
    thrd_detach(t);
    return MX_OK;
}

static int hid_input_devices_poll_thread(void* arg) {
    int dirfd = open(DEV_INPUT, O_DIRECTORY|O_RDONLY);
    if (dirfd < 0) {
        printf("hid: error opening %s\n", DEV_INPUT);
        return MX_ERR_INTERNAL;
    }
    mxio_watch_directory(dirfd, hid_input_device_added, MX_TIME_INFINITE, NULL);
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

    uint32_t tmp = 0xffffffff;
    if (argc > 1) {
        mx_status_t res = parse_uint_arg(argv[1], 0, 0xffffffff, &tmp);
        if (res != MX_OK) {
            printf("Failed to parse <num reads> (res %d)\n", res);
            usage();
            return 0;
        }
    }

    int fd = open(argv[0], O_RDWR);
    if (fd < 0) {
        printf("could not open %s: %d\n", argv[0], errno);
        return -1;
    }

    input_args_t* args = calloc(1, sizeof(*args));
    args->fd = fd;
    args->num_reads = tmp;

    strlcpy(args->name, argv[0], sizeof(args->name));
    thrd_t t;
    int ret = thrd_create_with_name(&t, hid_input_thread, (void*)args, args->name);
    if (ret != thrd_success) {
        printf("hid: input thread %s did not start (error=%d)\n", args->name, ret);
        free(args);
        close(fd);
        return -1;
    }
    thrd_join(t, NULL);
    return 0;
}

int readall_reports(int argc, const char** argv) {
    int ret = thrd_create_with_name(&input_poll_thread,
                                    hid_input_devices_poll_thread,
                                    NULL,
                                    "hid-inputdev-poll");
    if (ret != thrd_success) {
        return -1;
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

    input_get_report_size_t size_arg;
    mx_status_t res = parse_set_get_report_args(argc, argv, &size_arg.id, &size_arg.type);
    if (res != MX_OK) {
        printf("Failed to parse type/id for get report operation (res %d)\n", res);
        usage();
        return 0;
    }

    int fd = open(argv[0], O_RDWR);
    if (fd < 0) {
        printf("could not open %s: %d\n", argv[0], errno);
        return -1;
    }

    xprintf("hid: getting report size for id=0x%02x type=%u\n", size_arg.id, size_arg.type);

    input_report_size_t size;
    ssize_t rc = ioctl_input_get_report_size(fd, &size_arg, &size);
    if (rc < 0) {
        printf("hid: could not get report (id 0x%02x type %u) size from %s (status=%zd)\n",
                size_arg.id, size_arg.type, argv[0], rc);
        return rc;
    }
    xprintf("hid: report size=%u\n", size);

    input_get_report_t rpt_arg;
    rpt_arg.id = size_arg.id;
    rpt_arg.type = size_arg.type;

    // TODO(johngro) : Come up with a better policy than this...  While devices
    // are *supposed* to only deliver a report descriptor's computed size, in
    // practice they frequently seem to deliver number of bytes either greater
    // or fewer than the number of bytes originally requested.  For example...
    //
    // ++ Sometimes a device is expected to deliver a Report ID byte along with
    //    the payload contents, but does not do so.
    // ++ Sometimes it is unclear whether or not a device needs to deliver a
    //    Report ID byte at all since there is only one report listed (and,
    //    sometimes the device delivers that ID, and sometimes it chooses not
    //    to).
    // ++ Sometimes no bytes at all are returned for a report (this seems to
    //    be relatively common for input reports)
    // ++ Sometimes the number of bytes returned has basically nothing to do
    //    with the expected size of the report (this seems to be relatively
    //    common for vendor feature reports).
    //
    // Because of this uncertainty, we currently just provide a worst-case 4KB
    // buffer to read into, and report the number of bytes which came back along
    // with the expected size of the raw report.
    uint8_t* buf = malloc(4u << 10);
    rc = ioctl_input_get_report(fd, &rpt_arg, buf, sizeof(buf));
    if (rc < 0) {
        printf("hid: could not get report: %zd\n", rc);
    } else {
        printf("hid: got %zu bytes (raw report size %u)\n", rc, size);
        print_hex(buf, rc);
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

    input_get_report_size_t size_arg;
    mx_status_t res = parse_set_get_report_args(argc, argv, &size_arg.id, &size_arg.type);
    if (res != MX_OK) {
        printf("Failed to parse type/id for get report operation (res %d)\n", res);
        usage();
        return 0;
    }

    xprintf("hid: getting report size for id=0x%02x type=%u\n", size_arg.id, size_arg.type);

    input_set_report_t* arg = NULL;
    int fd = open(argv[0], O_RDWR);
    if (fd < 0) {
        printf("could not open %s: %d\n", argv[0], errno);
        return -1;
    }

    input_report_size_t size;
    ssize_t rc = ioctl_input_get_report_size(fd, &size_arg, &size);
    if (rc < 0) {
        printf("hid: could not get report (id 0x%02x type %u) size from %s (status=%zd)\n",
                size_arg.id, size_arg.type, argv[0], rc);
        goto finished;
    }

    // If the set/get report args parsed, then we must have at least 3 arguments.
    MX_DEBUG_ASSERT(argc >= 3);
    input_report_size_t payload_size = argc - 3;

    xprintf("hid: report size=%u, tx payload size=%u\n", size, payload_size);

    size_t in_len = sizeof(input_set_report_t) + payload_size;
    arg = malloc(in_len);
    arg->id = size_arg.id;
    arg->type = size_arg.type;
    for (int i = 0; i < payload_size; i++) {
        uint32_t tmp;
        mx_status_t res = parse_uint_arg(argv[i+3], 0, 255, &tmp);
        if (res != MX_OK) {
            printf("Failed to parse payload byte \"%s\" (res = %d)\n", argv[i+3], res);
            rc = res;
            goto finished;
        }

        arg->data[i] = tmp;
    }

    rc = ioctl_input_set_report(fd, arg, in_len);
    if (rc < 0) {
        printf("hid: could not set report: %zd\n", rc);
    } else {
        printf("hid: success\n");
    }

finished:
    if (arg) { free(arg); }
    close(fd);
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
