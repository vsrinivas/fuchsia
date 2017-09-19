// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/device/input.h>
#include <zircon/types.h>

#define CLEAR_SCREEN printf("\033[2J")
#define CURSOR_MOVE(r, c) printf("\033[%d;%dH", r, c)
#define CLEAR_LINE printf("\033[2K")

static void process_sensor_input(void* buf, size_t len) {
    uint8_t* report = buf;
    if (len < 1) {
        printf("bad report size: %zd < %d\n", len, 1);
        return;
    }


    uint8_t report_id = report[0];
    CURSOR_MOVE(report_id + 1, 0);
    CLEAR_LINE;

    // TODO(teisenbe): Once we can decode these reports, output them decoded.
    printf("%3d:", report_id);
    for (size_t i = 1; i < len; ++i) {
        printf(" %02x", report[i]);
    }
    printf("\n");
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s /dev/class/input/<id>\n", argv[0]);
        return -1;
    }

    const char* devname = argv[1];
    int fd = open(devname, O_RDONLY);
    if (fd < 0) {
        printf("failed to open %s: %d\n", devname, errno);
        return -1;
    }

    size_t rpt_desc_len = 0;
    uint8_t* rpt_desc = NULL;

    ssize_t ret = ioctl_input_get_report_desc_size(fd, &rpt_desc_len);
    if (ret < 0) {
        printf("failed to get report descriptor length for %s: %zd\n", devname, ret);
        return -1;
    }

    rpt_desc = malloc(rpt_desc_len);
    if (rpt_desc == NULL) {
        printf("no memory!\n");
        return -1;
    }

    ret = ioctl_input_get_report_desc(fd, rpt_desc, rpt_desc_len);
    if (ret < 0) {
        printf("failed to get report descriptor for %s: %zd\n", devname, ret);
        return -1;
    }

    assert(rpt_desc_len > 0);
    assert(rpt_desc);

    input_report_size_t max_rpt_sz = 0;
    ret = ioctl_input_get_max_reportsize(fd, &max_rpt_sz);
    if (ret < 0) {
        printf("failed to get max report size: %zd\n", ret);
        return -1;
    }
    void* buf = malloc(max_rpt_sz);
    if (buf == NULL) {
        printf("no memory!\n");
        return -1;
    }

    CLEAR_SCREEN;
    fflush(stdout);
    while (1) {
        ssize_t r = read(fd, buf, max_rpt_sz);
        if (r < 0) {
            printf("sensor read error: %zd (errno=%d)\n", r, errno);
            break;
        }

        process_sensor_input(buf, r);
    }

    free(buf);
    free(rpt_desc);
    close(fd);
    return 0;
}
