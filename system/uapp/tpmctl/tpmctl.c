// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/tpm.h>
#include <errno.h>
#include <fcntl.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fdio/util.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char* prog_name;

void print_usage(void) {
    printf("Usage:\n");
    printf("\n");
    printf("%s save\n", prog_name);
    printf("save: Issue a TPM_SaveState command.\n");
}

int cmd_save_state(int fd, int argc, const char** argv) {
    ssize_t ret = ioctl_tpm_save_state(fd);
    if (ret < 0) {
        printf("Error when saving state: (%zd)\n", ret);
        return 1;
    }

    return 0;
}

int main(int argc, const char** argv) {
    if (argc < 1)
        return 1;

    prog_name = argv[0];

    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char* cmd = argv[1];

    argc -= 2;
    argv += 2;

    int fd = open("/dev/class/tpm/000", O_RDWR);
    if (fd < 0) {
        printf("Error opening TPM device.\n");
        return 1;
    }

    if (!strcmp("save", cmd)) {
        return cmd_save_state(fd, argc, argv);
    } else {
        printf("Unrecognized command %s.\n", cmd);
        print_usage();
        return 1;
    }

    printf("We should never get here!.\n");
    return 1;
}
