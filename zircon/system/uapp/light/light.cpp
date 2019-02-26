// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <fuchsia/hardware/light/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

static int name_command(zx_handle_t svc, int argc, const char* argv[]) {
    if (argc != 1) {
        fprintf(stderr, "expected one argument\n");
        return -1;
    }
    uint32_t index;
    if (sscanf(argv[0], "%u", &index) != 1) {
        fprintf(stderr, "could not parse index %s\n", argv[0]);
        return -1;
    }

    char buffer[fuchsia_hardware_light_LIGHT_NAME_LEN];
    size_t actual;
    zx_status_t status2;
    auto status = fuchsia_hardware_light_LightGetName(svc, index, &status2, buffer, sizeof(buffer),
                                                      &actual);
    if (status == ZX_OK) {
        status = status2;
    }
    if (status == ZX_OK) {
        printf("%s\n", buffer);
        return 0;
    } else {
        fprintf(stderr, "fuchsia_hardware_light_DeviceGetName failed: %s\n",
                zx_status_get_string(status));
        return -1;
    }
}

static int count_command(zx_handle_t svc, int argc, const char* argv[]) {
    uint32_t count;
    auto status = fuchsia_hardware_light_LightGetCount(svc, &count);
    if (status == ZX_OK) {
        printf("%u\n", count);
        return 0;
    } else {
        fprintf(stderr, "fuchsia_hardware_light_DeviceGetCount failed: %s\n",
                zx_status_get_string(status));
        return -1;
    }
}

static int has_capability_command(zx_handle_t svc, int argc, const char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "expected two arguments\n");
        return -1;
    }
    uint32_t index;
    if (sscanf(argv[0], "%u", &index) != 1) {
        fprintf(stderr, "could not parse index %s\n", argv[0]);
        return -1;
    }
    fuchsia_hardware_light_Capability capability;
    if (!strcmp(argv[1], "brightness")) {
        capability = fuchsia_hardware_light_Capability_BRIGHTNESS;
    } else if (!strcmp(argv[1], "rgb")) {
        capability = fuchsia_hardware_light_Capability_RGB;
    } else {
        fprintf(stderr, "unknown capability \"%s\"\n", argv[1]);
        return -1;
    }

    zx_status_t status2;
    bool has;
    auto status = fuchsia_hardware_light_LightHasCapability(svc, index, capability, &status2, &has);
    if (status == ZX_OK) {
        status = status2;
    }
    if (status == ZX_OK) {
        printf("%s\n", (has ? "true" : "false"));
        return 0;
    } else {
        fprintf(stderr, "fuchsia_hardware_light_LightHasCapability failed: %s\n",
                zx_status_get_string(status));
        return -1;
    }
}

static int get_value_command(zx_handle_t svc, int argc, const char* argv[]) {
    if (argc != 1) {
        fprintf(stderr, "expected one argument\n");
        return -1;
    }
    uint32_t index;
    if (sscanf(argv[0], "%u", &index) != 1) {
        fprintf(stderr, "could not parse index %s\n", argv[0]);
        return -1;
    }

    zx_status_t status2;
    uint8_t value;
    auto status = fuchsia_hardware_light_LightGetSimpleValue(svc, index, &status2, &value);
    if (status == ZX_OK) {
        status = status2;
    }
    if (status == ZX_OK) {
        printf("%u\n", value);
        return 0;
    } else {
        fprintf(stderr, "fuchsia_hardware_light_LightGetSimpleValue failed: %s\n",
                zx_status_get_string(status));
        return -1;
    }
}

static int set_value_command(zx_handle_t svc, int argc, const char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "expected two arguments\n");
        return -1;
    }
    uint32_t index;
    if (sscanf(argv[0], "%u", &index) != 1) {
        fprintf(stderr, "could not parse index %s\n", argv[0]);
        return -1;
    }
    uint32_t value;
    if (sscanf(argv[1], "%u", &value) != 1) {
        fprintf(stderr, "could not parse value %s\n", argv[1]);
        return -1;
    }
    if (value >= UINT8_MAX) {
        fprintf(stderr, "value %u out of range\n", value);
        return -1;
    }

    zx_status_t status2;
    auto status = fuchsia_hardware_light_LightSetSimpleValue(svc, index,
                                                             static_cast<uint8_t>(value), &status2);
    if (status == ZX_OK) {
        status = status2;
    }
    if (status == ZX_OK) {
        return 0;
    } else {
        fprintf(stderr, "fuchsia_hardware_light_LightSetSimpleValue failed: %s\n",
                zx_status_get_string(status));
        return -1;
    }
}

struct Command {
    const char* name;
    int (*command)(zx_handle_t svc, int argc, const char* argv[]);
    const char* description;
};

static Command commands[] = {
    {
        "name",
        name_command,
        "name <index> - returns the name of the light"
    },
    {
        "count",
        count_command,
        "count - returns the number of physical lights"
    },
    {
        "capability",
        has_capability_command,
        "capability <index> [brightness|rgb] - returns true if the light has the capability"
    },
    {
        "get-value",
        get_value_command,
        "get-value <index> - returns the current value of the light"
    },
    {
        "set-value",
        set_value_command,
        "set-value <index> <value> - sets the current value of the light"
    },
    {},
};

static void usage(void) {
    fprintf(stderr, "usage: \"light [-d <dev-file>] <command>\", where command is one of:\n");

    Command* command = commands;
    while (command->name) {
        fprintf(stderr, "    %s\n", command->description);
        command++;
    }
}

int main(int argc, const char** argv) {
    if (argc < 2) {
        usage();
        return -1;
    }
    argv++;
    argc--;

    const char* dev_file_name = "000";
    if (!strcmp(argv[0], "-d")) {
        if (argc < 3) {
            usage();
            return -1;
        }

        dev_file_name = argv[1];
        if (strlen(dev_file_name) != 3) {
            usage();
            return -1;
        }

        argv += 2;
        argc -= 2;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/dev/class/light/%s", dev_file_name);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("Error opening %s\n", path);
        return -1;
    }

    zx_handle_t svc;
    zx_status_t status = fdio_get_service_handle(fd, &svc);
    if (status != ZX_OK) {
        close(fd);
        printf("Error opening FIDL connection for %s\n", path);
        return -1;
    }
    auto cleanup = fbl::MakeAutoCall([fd, svc]() {zx_handle_close(svc); close(fd); });

    const char* command_name = argv[0];
    argv++;
    argc--;
    Command* command = commands;
    while (command->name) {
        if (!strcmp(command_name, command->name)) {
            return command->command(svc, argc, argv);
        }
        command++;
    }
    // if we fall through, print usage
    usage();
    return -1;
}
