// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <zircon/device/audio-codec.h>

static int cmd_enable(const char* dev, bool enable) {
    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        printf("Error opening %s\n", dev);
        return fd;
    }

    ssize_t rc = ioctl_audio_codec_enable(fd, &enable);
    if (rc < 0) {
        printf("Error enabling for %s (rc %zd)\n", dev, rc);
        close(fd);
        goto out;
    }

out:
    close(fd);
    return rc;
}

int main(int argc, const char** argv) {
    int rc = 0;
    const char *cmd = argc > 1 ? argv[1] : NULL;
    if (cmd) {
        if (!strcmp(cmd, "help")) {
            goto usage;
        } else if (!strcmp(cmd, "enable")) {
            if (argc < 3) goto usage;
            rc = cmd_enable(argv[2], true);
        } else if (!strcmp(cmd, "disable")) {
            if (argc < 3) goto usage;
            rc = cmd_enable(argv[2], false);
        } else {
            printf("Unrecognized command %s!\n", cmd);
            goto usage;
        }
    } else {
        goto usage;
    }
    return rc;
usage:
    printf("Usage:\n");
    printf("%s\n", argv[0]);
    printf("%s enable <codecdev>\n", argv[0]);
    printf("%s disable <codecdev>\n", argv[0]);
    return 0;
}
