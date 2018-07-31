// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gpt/gpt.h>
#include <zircon/types.h>
#include <lib/fdio/io.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <zircon/device/block.h>
#include <zircon/device/skip-block.h>
#include <zircon/device/device.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <pretty/hexdump.h>

#define DEV_BLOCK "/dev/class/block"
#define DEV_SKIP_BLOCK "/dev/class/skip-block"

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
    } else if (!strcmp(GUID_INSTALL_STRING, guid)) {
        return "fuchsia-install";
    } else if (!strcmp(GUID_BLOB_STRING, guid)) {
        return "fuchsia-blob";
    } else if (!strcmp(GUID_FVM_STRING, guid)) {
        return "fuchsia-fvm";
    } else if (!strcmp(GUID_ZIRCON_A_STRING, guid)) {
        return "zircon-a";
    } else if (!strcmp(GUID_ZIRCON_B_STRING, guid)) {
        return "zircon-b";
    } else if (!strcmp(GUID_ZIRCON_R_STRING, guid)) {
        return "zircon-r";
    } else if (!strcmp(GUID_SYS_CONFIG_STRING, guid)) {
        return "sys-config";
    } else if (!strcmp(GUID_FACTORY_CONFIG_STRING, guid)) {
        return "factory";
    } else if (!strcmp(GUID_BOOTLOADER_STRING, guid)) {
        return "bootloader";
    } else {
        return "unknown";
    }
}

typedef struct blkinfo {
    char path[128];
    char topo[1024];
    char guid[GPT_GUID_STRLEN];
    char label[40];
    char sizestr[6];
} blkinfo_t;

static int cmd_list_blk(void) {
    struct dirent* de;
    DIR* dir = opendir(DEV_BLOCK);
    if (!dir) {
        fprintf(stderr, "Error opening %s\n", DEV_BLOCK);
        return -1;
    }
    blkinfo_t info;
    const char* type;
    int fd;
    printf("%-3s %-4s %-16s %-20s %-6s %s\n",
           "ID", "SIZE", "TYPE", "LABEL", "FLAGS", "DEVICE");
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        memset(&info, 0, sizeof(blkinfo_t));
        type = NULL;
        snprintf(info.path, sizeof(info.path), "%s/%s", DEV_BLOCK, de->d_name);
        fd = open(info.path, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Error opening %s\n", info.path);
            goto devdone;
        }
        if (ioctl_device_get_topo_path(fd, info.topo, sizeof(info.topo)) < 0) {
            strcpy(info.topo, "UNKNOWN");
        }

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
            strlcat(flags, "RE ", sizeof(flags));
        }
        if (block_info.flags & BLOCK_FLAG_BOOTPART) {
            strlcat(flags, "BP ", sizeof(flags));
        }
        close(fd);
devdone:
        printf("%-3s %4s %-16s %-20s %-6s %s\n",
               de->d_name, info.sizestr, type ? type : "",
               info.label, flags, info.topo);
    }
    closedir(dir);
    return 0;
}

static int cmd_list_skip_blk(void) {
    struct dirent* de;
    DIR* dir = opendir(DEV_SKIP_BLOCK);
    if (!dir) {
        fprintf(stderr, "Error opening %s\n", DEV_SKIP_BLOCK);
        return -1;
    }
    blkinfo_t info;
    const char* type;
    int fd;
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        memset(&info, 0, sizeof(blkinfo_t));
        type = NULL;
        snprintf(info.path, sizeof(info.path), "%s/%s", DEV_SKIP_BLOCK, de->d_name);
        fd = open(info.path, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Error opening %s\n", info.path);
            goto devdone;
        }
        if (ioctl_device_get_topo_path(fd, info.topo, sizeof(info.topo)) < 0) {
            strcpy(info.topo, "UNKNOWN");
        }

        skip_block_partition_info_t partition_info;
        if (ioctl_skip_block_get_partition_info(fd, &partition_info) > 0) {
            size_to_cstring(info.sizestr, sizeof(info.sizestr),
                            partition_info.block_size_bytes * partition_info.partition_block_count);
            uint8_to_guid_string(info.guid, partition_info.partition_guid);
            type = guid_to_type(info.guid);
        }

        close(fd);
devdone:
        printf("%-3s %4s %-16s %-20s %-6s %s\n",
               de->d_name, info.sizestr, type ? type : "", "", "", info.topo);
    }
    closedir(dir);
    return 0;
}

static int try_read_skip_blk(int fd, off_t offset, size_t count) {
    // check that count and offset are aligned to block size
    uint64_t blksize;
    skip_block_partition_info_t info;
    ssize_t rc = ioctl_skip_block_get_partition_info(fd, &info);
    if (rc < (ssize_t)sizeof(info)) {
        return rc;
    }
    blksize = info.block_size_bytes;
    if (count % blksize) {
        fprintf(stderr, "Bytes read must be a multiple of blksize=%" PRIu64 "\n", blksize);
        return -1;
    }
    if (offset % blksize) {
        fprintf(stderr, "Offset must be a multiple of blksize=%" PRIu64 "\n", blksize);
        return -1;
    }

    // allocate and map a buffer to read into
    zx_handle_t vmo, dup;
    void* buf;
    if (zx_vmo_create(count, 0, &vmo) != ZX_OK) {
        fprintf(stderr, "No memory\n");
        return -1;
    }
    if (zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, count,
                ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, (uintptr_t*) &buf) != ZX_OK) {
        fprintf(stderr, "Failed to map vmo\n");
        rc = -1;
        goto out;
    }
    if (zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &dup) != ZX_OK) {
        fprintf(stderr, "Cannot duplicate handle\n");
        goto out2;
    }

    // read the data
    skip_block_rw_operation_t op = {
        .vmo = dup,
        .vmo_offset = 0,
        .block = offset / blksize,
        .block_count = count / blksize,
    };

    ssize_t s = ioctl_skip_block_read(fd, &op);
    if (s < 0) {
        fprintf(stderr, "Error %zd in ioctl_skip_block_read()\n", s);
        rc = s;
        goto out2;
    }

    hexdump8_ex(buf, count, offset);

out2:
    zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)buf, count);
out:
    zx_handle_close(vmo);
    return rc;
}

static int cmd_read_blk(const char* dev, off_t offset, size_t count) {
    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening %s\n", dev);
        return fd;
    }

    // check that count and offset are aligned to block size
    uint64_t blksize;
    block_info_t info;
    ssize_t rc = ioctl_block_get_info(fd, &info);
    if (rc < 0) {
        if (try_read_skip_blk(fd, offset, count) < 0) {
            fprintf(stderr, "Error getting block size for %s\n", dev);
        }
        goto out;
    }
    blksize = info.block_size;
    if (count % blksize) {
        fprintf(stderr, "Bytes read must be a multiple of blksize=%" PRIu64 "\n", blksize);
        rc = -1;
        goto out;
    }
    if (offset % blksize) {
        fprintf(stderr, "Offset must be a multiple of blksize=%" PRIu64 "\n", blksize);
        rc = -1;
        goto out;
    }

    // read the data
    void* buf = malloc(count);
    if (offset) {
        rc = lseek(fd, offset, SEEK_SET);
        if (rc < 0) {
            fprintf(stderr, "Error %zd seeking to offset %jd\n", rc, (intmax_t)offset);
            goto out2;
        }
    }
    ssize_t c = read(fd, buf, count);
    if (c < 0) {
        fprintf(stderr,"Error %zd in read()\n", c);
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

static int cmd_stats(const char* dev, bool clear) {
    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening %s\n", dev);
        return fd;
    }

    block_stats_t stats;
    ssize_t rc = ioctl_block_get_stats(fd, &clear, &stats);
    if (rc < 0) {
        fprintf(stderr, "Error getting stats for %s\n", dev);
        close(fd);
        goto out;
    }

    printf("max concurrent ops:        %zu\n", stats.max_concur);
    printf("max pending block ops:     %zu\n", stats.max_pending);
    printf("total submitted block ops: %zu\n", stats.total_ops);
    printf("total submitted blocks:    %zu\n", stats.total_blocks);
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
        } else if (!strcmp(cmd, "stats")) {
            if (argc < 4) goto usage;
            if (strcmp("true", argv[3]) && strcmp("false", argv[3])) goto usage;
            rc = cmd_stats(argv[2], !strcmp("true", argv[3]) ? true : false);
        } else {
            fprintf(stderr, "Unrecognized command %s!\n", cmd);
            goto usage;
        }
    } else {
        rc = cmd_list_blk() || cmd_list_skip_blk();
    }
    return rc;
usage:
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "%s\n", argv[0]);
    fprintf(stderr, "%s read <blkdev> <offset> <count>\n", argv[0]);
    fprintf(stderr, "%s stats <blkdev> <clear=true|false>\n", argv[0]);
    return 0;
}
