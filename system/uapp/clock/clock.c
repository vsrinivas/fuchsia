// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/rtc.h>

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

int usage(const char* cmd) {
    fprintf(
        stderr,
        "Interact with the real-time clock:\n"
        "   %s                              Print the time\n"
        "   %s --set YYYY-mm-ddThh:mm:ss    Set the time\n",
        cmd,
        cmd);
    return -1;
}

int open_rtc(int mode) {
    int rtc_fd = open("/dev/sys/acpi/rtc/rtc", mode);
    if (rtc_fd < 0) {
        printf("Can not open RTC device\n");
    }
    return rtc_fd;
}

int print_rtc(void) {
    int rtc_fd = open_rtc(O_RDONLY);
    if (rtc_fd < 0) {
        return -1;
    }
    rtc_t rtc;
    ssize_t n = ioctl_rtc_get(rtc_fd, &rtc);
    if (n < (ssize_t)sizeof(rtc_t)) {
        return -1;
    }
    printf(
        "%04d-%02d-%02dT%02d:%02d:%02d\n",
        rtc.year,
        rtc.month,
        rtc.day,
        rtc.hours,
        rtc.minutes,
        rtc.seconds);
    return 0;
}

int set_rtc(const char* time) {
    rtc_t rtc;
    int n = sscanf(
        time,
        "%04hd-%02hhd-%02hhdT%02hhd:%02hhd:%02hhd",
        &rtc.year,
        &rtc.month,
        &rtc.day,
        &rtc.hours,
        &rtc.minutes,
        &rtc.seconds);
    if (n != 6) {
        return -1;
    }
    int rtc_fd = open_rtc(O_WRONLY);
    if (rtc_fd < 0) {
        return -1;
    }
    ssize_t written = ioctl_rtc_set(rtc_fd, &rtc);
    return (written == sizeof(rtc)) ? 0 : written;
}

int main(int argc, char** argv) {
    const char* cmd = basename(argv[0]);
    static const struct option opts[] = {
        {"set", required_argument, NULL, 's'},
        {},
    };
    for (int opt; (opt = getopt_long(argc, argv, "", opts, NULL)) != -1;) {
        switch (opt) {
        case 's':
            return set_rtc(optarg);
        default:
            return usage(cmd);
        }
    }
    if (argc != 1) {
        return usage(cmd);
    }
    return print_rtc();
}
