// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/gpt.h>
#include <magenta/types.h>
#include <mxio/io.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <magenta/device/block.h>
#include <magenta/device/device.h>
#include <pretty/hexdump.h>

#define DEV_BLOCK "/dev/class/block"

static char* size_to_cstring(char* str, size_t maxlen, uint64_t size) {
    const char* unit;
    uint64_t div;
    if (size < 1024) {
        unit = "";
        div = 1;
    } else if (size >= 1024 && size < 1024 * 1024) {
        unit = "K";
        div = 1024;
    } else if (size >= 1024 * 1024 && size < 1024 * 1024 * 1024) {
        unit = "M";
        div = 1024 * 1024;
    } else if (size >= 1024 * 1024 * 1024 && size < 1024llu * 1024 * 1024 * 1024) {
        unit = "G";
        div = 1024 * 1024 * 1024;
    } else {
        unit = "T";
        div = 1024llu * 1024 * 1024 * 1024;
    }
    snprintf(str, maxlen, "%" PRIu64 "%s", size / div, unit);
    return str;
}

static const char* guid_to_type(char* guid) {
    if (!strcmp("FE3A2A5D-4F32-41A7-B725-ACCC3285A309", guid)) {
        return "cros kernel";
    } else if (!strcmp("3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC", guid)) {
        return "cros rootfs";
    } else if (!strcmp("2E0A753D-9E48-43B0-8337-B15192CB1B5E", guid)) {
        return "cros reserved";
    } else if (!strcmp("CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3", guid)) {
        return "cros firmware";
    } else if (!strcmp("C12A7328-F81F-11D2-BA4B-00A0C93EC93B", guid)) {
        return "efi system";
    } else if (!strcmp("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", guid)) {
        return "data";
    } else if (!strcmp("21686148-6449-6E6F-744E-656564454649", guid)) {
        return "bios";
    } else if (!strcmp(GUID_SYSTEM_STRING, guid)) {
        return "fuchsia-system";
    } else if (!strcmp(GUID_DATA_STRING, guid)) {
        return "fuchsia-data";
    } else if (!strcmp(GUID_BLOBFS_STRING, guid)) {
        return "fuchsia-blobfs";
    } else {
        return "unknown";
    }
}

typedef struct blkinfo {
    char path[128];
    char devname[128];
    char drvname[128];
    char guid[GPT_GUID_STRLEN];
    char label[40];
    char sizestr[6];
} blkinfo_t;

static int cmd_list_blk(void) {
    struct dirent* de;
    DIR* dir = opendir(DEV_BLOCK);
    if (!dir) {
        printf("Error opening %s\n", DEV_BLOCK);
        return -1;
    }
    blkinfo_t info;
    const char* type;
    int fd;
    printf("%-3s %-8s %-8s %-4s %-14s %-20s %s\n", "ID", "DEV", "DRV", "SIZE", "TYPE", "LABEL",
           "FLAGS");
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        memset(&info, 0, sizeof(blkinfo_t));
        type = NULL;
        snprintf(info.path, sizeof(info.path), "%s/%s", DEV_BLOCK, de->d_name);
        fd = open(info.path, O_RDONLY);
        if (fd < 0) {
            printf("Error opening %s\n", info.path);
            goto devdone;
        }
        ioctl_device_get_device_name(fd, info.devname, sizeof(info.devname));
        ioctl_device_get_driver_name(fd, info.drvname, sizeof(info.drvname));

        block_info_t block_info;
        if (ioctl_block_get_info(fd, &block_info) > 0) {
            size_to_cstring(info.sizestr, sizeof(info.sizestr),
                            block_info.block_size * block_info.block_count);
        }
        uint8_t guid[GPT_GUID_LEN];
        if (ioctl_block_get_type_guid(fd, guid, sizeof(guid)) >= 0) {
            uint8_to_guid_string(info.guid, guid);
            type = guid_to_type(info.guid);
        }
        ioctl_block_get_name(fd, info.label, sizeof(info.label));

        char flags[20] = {0};

        if (block_info.flags & BLOCK_FLAG_READONLY) {
            strlcat(flags, "RO ", sizeof(flags));
        }
        if (block_info.flags & BLOCK_FLAG_REMOVABLE) {
            strlcat(flags, "REMOVABLE ", sizeof(flags));
        }
devdone:
        close(fd);
        printf("%-3s %-8s %-8s %4s %-14s %-20s %s\n", de->d_name, info.devname, info.drvname,
               info.sizestr, type ? type : "", info.label, flags);
    }
out:
    closedir(dir);
    return 0;
}

static int cmd_read_blk(const char* dev, off_t offset, size_t count) {
    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        printf("Error opening %s\n", dev);
        return fd;
    }

    // check that count and offset are aligned to block size
    uint64_t blksize;
    block_info_t info;
    ssize_t rc = ioctl_block_get_info(fd, &info);
    if (rc < 0) {
        printf("Error getting block size for %s\n", dev);
        close(fd);
        goto out;
    }
    blksize = info.block_size;
    if (count % blksize) {
        printf("Bytes read must be a multiple of blksize=%" PRIu64 "\n", blksize);
        rc = -1;
        goto out;
    }
    if (offset % blksize) {
        printf("Offset must be a multiple of blksize=%" PRIu64 "\n", blksize);
        rc = -1;
        goto out;
    }

    // read the data
    void* buf = malloc(count);
    if (offset) {
        rc = lseek(fd, offset, SEEK_SET);
        if (rc < 0) {
            printf("Error %zd seeking to offset %jd\n", rc, (intmax_t)offset);
            goto out2;
        }
    }
    ssize_t c = read(fd, buf, count);
    if (c < 0) {
        printf("Error %zd in read()\n", c);
        rc = c;
        goto out2;
    }

    hexdump8_ex(buf, c, offset);

out2:
    free(buf);
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
        } else if (!strcmp(cmd, "read")) {
            if (argc < 5) goto usage;
            rc = cmd_read_blk(argv[2], strtoul(argv[3], NULL, 10), strtoull(argv[4], NULL, 10));
        } else {
            printf("Unrecognized command %s!\n", cmd);
            goto usage;
        }
    } else {
        rc = cmd_list_blk();
    }
    return rc;
usage:
    printf("Usage:\n");
    printf("%s\n", argv[0]);
    printf("%s read <blkdev> <offset> <count>\n", argv[0]);
    return 0;
}
