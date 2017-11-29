// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/device/block.h>
#include <zircon/syscalls.h>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>

int main(int argc, char** argv) {
    printf("Executing dry run of GPT reconfig, layout will not be altered.\n");
    if (argc < 2) {
      printf("Disk path must be suppled.\n");
      return -1;
    }
    const char* dev_path = argv[1];
    int gpt_fd = open(dev_path, O_RDWR);
    if (gpt_fd < 0) {
        fprintf(stderr, "Failed to open block device.\n");
        return -1;
    }

    block_info_t blk_info;
    ssize_t rc = ioctl_block_get_info(gpt_fd, &blk_info);
    if (rc < 0) {
        printf("Error getting block info\n");
        close(gpt_fd);
        return -1;
    }

    gpt_device_t* dev;
    if (!gpt_device_read_gpt(gpt_fd, &dev)) {
        fprintf(stderr, "Error reading gpt\n");
        close(gpt_fd);
        return -1;
    }

    bool is_chromeos = is_cros(dev);
    if (is_chromeos) {
        printf("Looks like a chrome os device!\n");
    } else {
        close(gpt_fd);
        gpt_device_release(dev);
        printf("This doesn't look like a chromeos device.\n");
        return -1;
    }

    zx_status_t zc = config_cros_for_fuchsia(dev, &blk_info, SZ_ZX_PART,
                                             SZ_ROOT_PART, true);
    print_table(dev);

    if (zc == ZX_OK) {
        printf("Woohoo, dry run succeeded!\n");
    } else {
        printf("Reconfiguration dry run failed: %i\n", zc);
    }

    close(gpt_fd);
    gpt_device_release(dev);
    return 0;
}
