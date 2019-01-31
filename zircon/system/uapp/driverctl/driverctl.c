// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <zircon/device/device.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr,
            "Usage: driverctl <path> <command> [options]\n"
            "\n"
            "where path is path to driver file in /dev\n"
            "\n"
            "Command \"log\":\n"
            "  options are zero or more of:\n"
            "    \"error\" or \"e\":   DDK_LOG_ERROR\n"
            "    \"warn\" or \"w\":    DDK_LOG_WARN\n"
            "    \"info\" or \"i\":    DDK_LOG_INFO\n"
            "    \"trace\" or \"t\":   DDK_LOG_TRACE\n"
            "    \"spew\" or \"s\":    DDK_LOG_SPEW\n"
            "    \"debug1\" or \"d1\": DDK_LOG_DEBUG1\n"
            "    \"debug2\" or \"d2\": DDK_LOG_DEBUG2\n"
            "    \"debug3\" or \"d3\": DDK_LOG_DEBUG3\n"
            "    \"debug4\" or \"d4\": DDK_LOG_DEBUG4\n"
            "\n"
            "  With no options provided, driverctl log will print the current log flags for the driver.\n"
            "  A flag may have a '+' or '-' prepended. In that case the flag will be toggled\n"
            "  on (+) or off(-) without affecting other flags.\n"
            "  If toggled flags are used, all flags must be toggled.\n"
            "\n"
            "  Examples:\n"
            "\n"
            "  Set log flags to DDK_LOG_ERROR | DDK_LOG_INFO | DDK_LOG_TRACE:\n"
            "    $ driverctl <path> log error info trace\n"
            "  or:\n"
            "    $ driverctl <path> log e i t\n"
             "\n"
            "  Turn on DDK_LOG_TRACE and DDK_LOG_SPEW:\n"
            "    $ driverctl <path> log +trace +spew\n"
            "  or:\n"
            "    $ driverctl <path> log +t +s\n"
             "\n"
            "  Turn off DDK_LOG_SPEW:\n"
            "    $ driverctl <path> log -spew\n"
            "  or:\n"
            "    $ driverctl <path> log -s\n"
            );
}

int main(int argc, char **argv) {
    int ret = 0;

    if (argc < 3) {
        usage();
        return -1;
    }

    const char* path = argv[1];
    if (!strcmp(path, "-h")) {
        usage();
        return 0;
    }

    const char* command = argv[2];
    if (strcmp(command, "log")) {
        fprintf(stderr, "Unsupported command %s\n", command);
        usage();
        return -1;
    }

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "could not open %s\n", path);
        return -1;
    }

    if (argc == 3) {
        uint32_t flags;
        ret = ioctl_device_get_log_flags(fd, &flags);
        if (ret < 0) {
            fprintf(stderr, "ioctl_device_get_log_flags failed for %s\n", path);
        } else {
            printf("Log flags:");
            if (flags & DDK_LOG_ERROR) {
                printf(" ERROR");
            }
            if (flags & DDK_LOG_WARN) {
                printf(" WARN");
            }
            if (flags & DDK_LOG_INFO) {
                printf(" INFO");
            }
            if (flags & DDK_LOG_TRACE) {
                printf(" TRACE");
            }
            if (flags & DDK_LOG_SPEW) {
                printf(" SPEW");
            }
            if (flags & DDK_LOG_DEBUG1) {
                printf(" DEBUG1");
            }
            if (flags & DDK_LOG_DEBUG2) {
                printf(" DEBUG2");
            }
            if (flags & DDK_LOG_DEBUG3) {
                printf(" DEBUG3");
            }
            if (flags & DDK_LOG_DEBUG4) {
                printf(" DEBUG4");
            }
            printf("\n");
        }
        goto out;
    }

    driver_log_flags_t flags = {0, 0};
    char* toggle_arg = NULL;
    char* non_toggle_arg = NULL;

    for (int i = 3; i < argc; i++) {
        char* arg = argv[i];
        char toggle = arg[0];
        uint32_t flag = 0;

        // check for leading + or -
        if (toggle == '+' || toggle == '-') {
            toggle_arg = arg;
            arg++;
        } else {
            non_toggle_arg = arg;
        }

        if (toggle_arg && non_toggle_arg) {
            fprintf(stderr, "Cannot mix toggled flag \"%s\" with non-toggle flag \"%s\"\n",
                    toggle_arg, non_toggle_arg);
            usage();
            ret = -1;
            goto out;
        }

        if (!strcasecmp(arg, "e") || !strcasecmp(arg, "error")) {
            flag = DDK_LOG_ERROR;
        } else if (!strcasecmp(arg, "w") || !strcasecmp(arg, "warn")) {
            flag = DDK_LOG_WARN;
        } else if (!strcasecmp(arg, "i") || !strcasecmp(arg, "info")) {
            flag = DDK_LOG_INFO;
        } else if (!strcasecmp(arg, "t") || !strcasecmp(arg, "trace")) {
            flag = DDK_LOG_TRACE;
        } else if (!strcasecmp(arg, "s") || !strcasecmp(arg, "spew")) {
            flag = DDK_LOG_SPEW;
        } else if (!strcasecmp(arg, "d1") || !strcasecmp(arg, "debug1")) {
            flag = DDK_LOG_DEBUG1;
        } else if (!strcasecmp(arg, "d2") || !strcasecmp(arg, "debug2")) {
            flag = DDK_LOG_DEBUG2;
        } else if (!strcasecmp(arg, "d3") || !strcasecmp(arg, "debug3")) {
            flag = DDK_LOG_DEBUG3;
        } else if (!strcasecmp(arg, "d4") || !strcasecmp(arg, "debug4")) {
            flag = DDK_LOG_DEBUG4;
        } else {
            fprintf(stderr, "unknown flag %s\n", arg);
            ret = -1;
            goto out;
        }

        if (toggle == '+') {
            flags.set |= flag;
        } else if (toggle == '-') {
            flags.clear |= flag;
        } else {
            flags.set |= flag;
        }
    }

    if (!toggle_arg) {
        // clear all flags not explicitly set if we aren't using flag toggles
        flags.clear = ~flags.set;
    }

    ret = ioctl_device_set_log_flags(fd, &flags);
    if (ret < 0) {
        fprintf(stderr, "ioctl_device_set_log_flags failed for %s\n", path);
    }

out:
    close(fd);
    return ret;
}
