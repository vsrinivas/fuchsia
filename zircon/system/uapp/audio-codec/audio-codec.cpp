// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <fuchsia/hardware/audiocodec/c/fidl.h>
#include <string.h>
#include <unistd.h>
#include <zircon/status.h>

static zx_status_t cmd_enable(const char* dev, bool enable) {
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
        printf("Error creating channel: %s\n", zx_status_get_string(status));
        return status;
    }

    status = fdio_service_connect(dev, remote.release());
    if (status != ZX_OK) {
        printf("Error opening %s: %s\n", dev, zx_status_get_string(status));
        return status;
    }

    status = fuchsia_hardware_audiocodec_DeviceSetEnabled(local.get(), enable);
    if (status != ZX_OK) {
        printf("Error enabling for %s: %s\n", dev, zx_status_get_string(status));
        return status;
    }

    return ZX_OK;
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
