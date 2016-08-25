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

#define DEV_INPUT "/dev/class/input"

#define INPUT_LISTENER_FLAG_RUNNING 1

static bool verbose = false;
#define xprintf(fmt...) do { if (verbose) printf(fmt); } while (0)

void usage(void) {
    printf("usage: hid [-v] <command> [<args>]\n\n");
    printf("  commands:\n");
    printf("    read\n");
    printf("    status <devpath>\n");
    printf("    get <devpath> <in|out|feature> <id>\n");
    printf("    set <devpath> <in|out|feature> <id> [0xXX *]\n");
    printf("  all values are parsed as hexadecimal integers\n");
}

typedef struct {
    char dev_name[128];
    thrd_t t;
    int flags;
    int fd;
    struct list_node node;
} input_listener_t;

static struct list_node input_listeners_list = LIST_INITIAL_VALUE(input_listeners_list);
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
    int rc = mxio_ioctl(fd, IOCTL_INPUT_GET_PROTOCOL, NULL, 0, &proto, sizeof(proto));
    if (rc < 0) {
        lprintf("hid: could not get protocol from %s (status=%d)\n", name, rc);
    } else {
        lprintf("hid: %s proto=%d\n", name, proto);
    }
    return rc;
}

static int get_report_desc_len(int fd, const char* name, size_t* report_desc_len) {
    int rc = mxio_ioctl(fd, IOCTL_INPUT_GET_REPORT_DESC_SIZE, NULL, 0, report_desc_len, sizeof(*report_desc_len));
    if (rc < 0) {
        lprintf("hid: could not get report descriptor length from %s (status=%d)\n", name, rc);
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
    int rc = mxio_ioctl(fd, IOCTL_INPUT_GET_REPORT_DESC, NULL, 0, buf, report_desc_len);
    if (rc < 0) {
        lprintf("hid: could not get report descriptor from %s (status=%d)\n", name, rc);
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
    int rc = mxio_ioctl(fd, IOCTL_INPUT_GET_NUM_REPORTS, NULL, 0, num_reports, sizeof(*num_reports));
    if (rc < 0) {
        lprintf("hid: could not get number of reports from %s (status=%d)\n", name, rc);
    } else {
        lprintf("hid: %s num reports: %zu\n", name, *num_reports);
    }
    return rc;
}

static int get_report_ids(int fd, const char* name, size_t num_reports) {
    input_report_id_t* ids = malloc(num_reports * sizeof(input_report_id_t));
    if (!ids) return ERR_NO_MEMORY;

    int rc = mxio_ioctl(fd, IOCTL_INPUT_GET_REPORT_IDS, NULL, 0, ids, num_reports * sizeof(input_report_id_t));
    if (rc < 0) {
        lprintf("hid: could not get report ids from %s (status=%d)\n", name, rc);
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
        int size_rc = mxio_ioctl(fd, IOCTL_INPUT_GET_REPORT_SIZE, &arg, sizeof(arg), &size, sizeof(size));
        if (size_rc < 0) {
            printf("hid: could not get report id size from %s (status=%d)\n", name, size_rc);
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

static int get_max_report_len(int fd, const char* name, size_t* max_report_len) {
    size_t tmp;
    if (max_report_len == NULL) {
        max_report_len = &tmp;
    }
    int rc = mxio_ioctl(fd, IOCTL_INPUT_GET_MAX_REPORTSIZE, NULL, 0, max_report_len, sizeof(*max_report_len));
    if (rc < 0) {
        lprintf("hid: could not get max report size from %s (status=%d)\n", name, rc);
    } else {
        lprintf("hid: %s maxreport=%zu\n", name, *max_report_len);
    }
    return rc;
}

#define try(fn) \
    do { \
        int rc = fn; \
        if (rc < 0) return rc; \
    } while (0)

static int hid_status(int fd, const char* name, size_t* max_report_len) {
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
    input_listener_t* listener = arg;
    assert(listener->flags & INPUT_LISTENER_FLAG_RUNNING);
    assert(listener->fd >= 0);
    lprintf("hid: input thread started for %s\n", listener->dev_name);
    const char* name = listener->dev_name;
    int fd = listener->fd;

    size_t max_report_len = 0;
    try(hid_status(fd, name, &max_report_len));

    uint8_t* report = calloc(1, max_report_len);
    if (!report) return ERR_NO_MEMORY;

    for (;;) {
        mxio_wait_fd(fd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
        int r = read(fd, report, max_report_len);
        mtx_lock(&print_lock);
        printf("read returned %d\n", r);
        if (r < 0) {
            mtx_unlock(&print_lock);
            break;
        }
        printf("hid: input from %s\n", name);
        print_hex(report, r);
        mtx_unlock(&print_lock);
    }
    free(report);
    lprintf("hid: closing %s\n", name);
    close(fd);
    listener->fd = -1;
    listener->flags &= ~INPUT_LISTENER_FLAG_RUNNING;
    return NO_ERROR;
}

static int hid_input_devices_poll_thread(void* arg) {
    for (;;) {
        struct dirent* de;
        DIR* dir = opendir(DEV_INPUT);
        if (!dir) {
            printf("hid: error opening %s\n", DEV_INPUT);
            return ERR_INTERNAL;
        }
        char dname[128];
        char tname[128];
        while ((de = readdir(dir)) != NULL) {
            snprintf(dname, sizeof(dname), "%s/%s", DEV_INPUT, de->d_name);

            // is there already a listener for this device?
            bool found = false;
            input_listener_t* listener = NULL;
            list_for_every_entry(&input_listeners_list, listener, input_listener_t, node) {
                if ((listener->flags & INPUT_LISTENER_FLAG_RUNNING) && !strcmp(listener->dev_name, dname)) {
                    found = true;
                    break;
                }
            }
            if (found) continue;
            input_listener_t* free = NULL;
            list_for_every_entry(&input_listeners_list, listener, input_listener_t, node) {
                if (!(listener->flags & INPUT_LISTENER_FLAG_RUNNING)) {
                    free = listener;
                    break;
                }
            }
            if (!free) {
                free = calloc(1, sizeof(input_listener_t));
                if (!free) {
                    break;
                }
                list_add_tail(&input_listeners_list, &free->node);
            }
            free->flags |= INPUT_LISTENER_FLAG_RUNNING;
            strncpy(free->dev_name, dname, sizeof(free->dev_name));
            free->fd = open(free->dev_name, O_RDONLY);
            if (free->fd < 0) {
                free->flags &= ~INPUT_LISTENER_FLAG_RUNNING;
                continue;
            }
            snprintf(tname, sizeof(tname), "hid-input-%s", de->d_name);
            int ret = thrd_create_with_name(&free->t, hid_input_thread, (void*)free, tname);
            if (ret != thrd_success) {
                printf("hid: input thread %s did not start (error=%d)\n", tname, ret);
                free->flags &= ~INPUT_LISTENER_FLAG_RUNNING;
            }
        }
        closedir(dir);
        usleep(1000 * 1000);
    }
    return NO_ERROR;
}

int read_reports(int argc, const char** argv) {
    int ret = thrd_create_with_name(&input_poll_thread, hid_input_devices_poll_thread, NULL, "hid-inputdev-poll");
    if (ret != thrd_success) {
        return ret;
    }

    thrd_join(input_poll_thread, NULL);
    return 0;
}

int get_status(int argc, const char** argv) {
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
    return hid_status(fd, argv[0], NULL);
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

    input_get_report_size_t arg;
    arg.id = strtoul(argv[2], NULL, 16);
    arg.type = strtoul(argv[1], NULL, 16);
    xprintf("hid: getting report size for id=%u type=%u\n", arg.id, arg.type);

    input_report_size_t size;
    int rc = mxio_ioctl(fd, IOCTL_INPUT_GET_REPORT_SIZE, &arg, sizeof(arg), &size, sizeof(size));
    if (rc < 0) {
        printf("hid: could not get report id size from %s (status=%d)\n", argv[0], rc);
        return rc;
    }
    xprintf("hid: report size=%u\n", size);

    uint8_t* buf = malloc(size);
    rc = mxio_ioctl(fd, IOCTL_INPUT_GET_REPORT, &arg, sizeof(arg), buf, size);
    if (rc < 0) {
        printf("hid: could not get report: %d\n", rc);
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
    int rc = mxio_ioctl(fd, IOCTL_INPUT_GET_REPORT_SIZE, &size_arg, sizeof(size_arg), &size, sizeof(size));
    if (rc < 0) {
        printf("hid: could not get report id size from %s (status=%d)\n", argv[0], rc);
        return rc;
    }
    xprintf("hid: report size=%u\n", size);
    if (argc - 3 < size) {
        printf("not enough data for set\n");
        return -1;
    } else if (argc - 3 > size) {
        printf("ignoring extra data\n");
    }

    input_set_report_t* arg = malloc(sizeof(input_set_report_t) + size);
    arg->id = size_arg.id;
    arg->type = size_arg.type;
    for (int i = 0; i < size; i++) {
        arg->data[i] = strtoul(argv[i+3], NULL, 16);
    }
    rc = mxio_ioctl(fd, IOCTL_INPUT_SET_REPORT, arg, sizeof(input_set_report_t) + size, NULL, 0);
    if (rc < 0) {
        printf("hid: could not set report: %d\n", rc);
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
    if (!strcmp("read", argv[0])) return read_reports(argc, argv);
    if (!strcmp("status", argv[0])) return get_status(argc, argv);
    if (!strcmp("get", argv[0])) return get_report(argc, argv);
    if (!strcmp("set", argv[0])) return set_report(argc, argv);
    usage();
    return 0;
}
