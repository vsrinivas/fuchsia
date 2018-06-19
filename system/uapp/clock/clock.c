// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/rtc.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

int usage(const char* cmd) {
    fprintf(
        stderr,
        "Interact with the real-time clock:\n"
        "   %s                              Print the time\n"
        "   %s --help                       Print this message\n"
        "   %s --set YYYY-mm-ddThh:mm:ss    Set the time\n"
        "   optionally specify an RTC device with --dev PATH_TO_DEVICE_NODE\n",
        cmd,
        cmd,
        cmd);
    return -1;
}

char *guess_dev(void) {
    char path[19]; // strlen("/dev/class/rtc/###") + 1
    DIR *d = opendir("/dev/class/rtc");
    if (!d) {
        return NULL;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strlen(de->d_name) != 3) {
            continue;
        }

        if (isdigit(de->d_name[0]) &&
            isdigit(de->d_name[1]) &&
            isdigit(de->d_name[2])) {
            sprintf(path, "/dev/class/rtc/%.3s", de->d_name);
            closedir(d);
            return strdup(path);
        }
    }

    closedir(d);
    return NULL;
}

int open_rtc(const char *path, int mode) {
    int rtc_fd = open(path, mode);
    if (rtc_fd < 0) {
        printf("Can not open RTC device\n");
    }
    return rtc_fd;
}

int print_rtc(const char *path) {
    int rtc_fd = open_rtc(path, O_RDONLY);
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

int set_rtc(const char *path, const char* time) {
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
        printf("Bad time format.\n");
        return -1;
    }
    int rtc_fd = open_rtc(path, O_WRONLY);
    if (rtc_fd < 0) {
        printf("Can not open RTC device\n");
        return -1;
    }
    ssize_t written = ioctl_rtc_set(rtc_fd, &rtc);
    return (written == sizeof(rtc)) ? 0 : written;
}

int main(int argc, char** argv) {
    int err;
    const char* cmd = basename(argv[0]);
    char *path = NULL;
    char *set = NULL;
    static const struct option opts[] = {
        {"set",  required_argument, NULL, 's'},
        {"dev",  required_argument, NULL, 'd'},
        {"help", no_argument,       NULL, 'h'},
        {0},
    };
    for (int opt; (opt = getopt_long(argc, argv, "", opts, NULL)) != -1;) {
        switch (opt) {
        case 's':
            set = strdup(optarg);
            break;
        case 'd':
            path = strdup(optarg);
            break;
        case 'h':
            usage(cmd);
            err = 0;
            goto done;
        default:
            err = usage(cmd);
            goto done;
        }
    }

    argv += optind;
    argc -= optind;

    if (argc != 0) {
        err = usage(cmd);
        goto done;
    }

    if (!path) {
        path = guess_dev();
        if (!path) {
            fprintf(stderr, "No RTC found.\n");
            err = usage(cmd);
            goto done;
        }
    }

    if (set) {
        err = set_rtc(path, set);
        if (err) {
            printf("Set RTC failed.\n");
            usage(cmd);
        }
        goto done;
    }

    err = print_rtc(path);
    if (err) {
        usage(cmd);
    }

done:
    free(path);
    free(set);
    return err;
}
