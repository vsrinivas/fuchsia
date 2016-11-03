// Copyright 2016 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gpt/gpt.h>
#include <inttypes.h>
#include <limits.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <magenta/device/block.h>
#include <mxio/io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_BLOCKDEV "/dev/class/block/000"
#define PATH_BLOCKDEVS "/dev/class/block"

// 4GB
#define MIN_SIZE_FUCHSIA_PART (((uint64_t)1024u) * 1024u * 1024u * 4u)
// 1GB
#define MIN_SIZE_MAGENTA_PART (((uint64_t)1024u) * 1024u * 1024u)

#define PATH_MAX 4096

// use for the partition mask sent to parition_for_install
typedef enum {
    PART_MAGENTA = 1 << 0,
    PART_FUCHSIA = 1 << 1
} partition_flags;

// GUID for a fuchsia partition
static const uint8_t guid_fuchsia_part[16] = {
    0x0b, 0x00, 0x6b, 0x60,
    0xc7, 0xb7,
    0x53, 0x46,
    0xa7, 0xd5,
    0xb7, 0x37, 0x33, 0x2c, 0x89, 0x9d
};

// GUID for an EFI partition
static const uint8_t guid_efi_part[16] = {
    0x28, 0x73, 0x2a, 0xc1,
    0x1f, 0xf8,
    0xd2, 0x11,
    0xba, 0x4b,
    0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b
};

/*
 * Read the next entry out of the directory and place copy it into name_out.
 *
 * Return the difference between the length of the name and the number of bytes
 * we did or would be able to copy.
 *  * -1 if there are no more entries
 *  * 0 if an entry is successfully read and all its bytes were copied into
 *      name_out
 *  * A positive value, which is the amount by which copying the directory name
 *      would exceed the limit expressed by max_name_len.
 */
static ssize_t get_next_file_path(
    DIR* dfd, size_t max_name_len, char* name_out) {
    DEBUG_ASSERT(name_out != NULL);
    struct dirent* entry = readdir(dfd);
    if (entry == NULL) {
        return -1;
    } else {
        size_t len_d_name = strlen(entry->d_name);
        ssize_t overrun = len_d_name - max_name_len + 1;
        if (overrun > 0) {
            ASSERT(overrun <= SSIZE_MAX);
            return overrun;
        } else {
            // copy the string, including the null terminator
            memcpy(name_out, entry->d_name, len_d_name + 1);
            return 0;
        }
    }
}

/*
 * Attempt to open the given path. If successful, returns the file descriptor
 * associated with the opened path in read only mode.
 */
static int open_device_ro(const char* dev_path) {
    int fd = open(dev_path, O_RDONLY);
    if (fd < 0) {
        printf("Could not read device at %s, open reported error:%s\n",
               dev_path, strerror(errno));
    }

    return fd;
}

/*
 * Attempt to read a GPT from the file descriptor. If successful the returned
 * pointer points to the populated gpt_device_t struct, otherwise NULL is
 * returned.
 */
static gpt_device_t* read_gpt(int fd, uint64_t* blocksize_out) {
    DEBUG_ASSERT(blocksize_out != NULL);
    ssize_t rc = ioctl_block_get_blocksize(fd, blocksize_out);
    if (rc < 0) {
        printf("error getting block size, ioctl result code: %zd\n", rc);
        return NULL;
    }

    if (*blocksize_out < 1) {
        printf("Device reports block size of %" PRIu64 ", abort!\n",
               *blocksize_out);
        return NULL;
    }

    uint64_t blocks;
    rc = ioctl_block_get_size(fd, &blocks);
    if (rc < 0) {
        printf("error getting device size, ioctl result code: %zd\n", rc);
        return NULL;
    }

    blocks /= *blocksize_out;

    printf("blocksize=%" PRIu64 " blocks=%" PRIu64 "\n", *blocksize_out,
           blocks);

    gpt_device_t* gpt;
    rc = gpt_device_init(fd, *blocksize_out, blocks, &gpt);
    if (rc < 0) {
        printf("error reading GPT, result code: %zd \n", rc);
        return NULL;
    } else if (!gpt->valid) {
        printf("error reading GPT, libgpt reports data is invalid\n");
        return NULL;
    } else {
        printf("read GPT\n");
    }

    return gpt;
}

/*
 * Given a pointer to GPT information, look for a partition with the matching
 * type GUID.
 * Returns
 *   * ERR_INVALID_ARGS if any pointers are null
 *   * ERR_BAD_STATE if the GPT data reports itself as invalid
 *   * ERR_NOT_FOUND if the requested partition is not present
 *   * NO_ERROR if the partition is found, in which case the index is assigned
 *     to part_id_out
 */
static mx_status_t find_partition(gpt_device_t* gpt_data, const uint8_t* guid,
                                  uint16_t* part_id_out) {
    DEBUG_ASSERT(gpt_data != NULL);
    DEBUG_ASSERT(guid != NULL);
    DEBUG_ASSERT(part_id_out != NULL);

    if (!gpt_data->valid) {
        return ERR_BAD_STATE;
    }

    for (uint16_t ptr = 0;
         ptr < countof(gpt_data->partitions) && gpt_data->partitions[ptr] != NULL;
         ptr++) {

        uint8_t* type_ptr = gpt_data->partitions[ptr]->type;
        if (!memcmp(type_ptr, guid, 16)) {
            *part_id_out = ptr;
            return NO_ERROR;
        }
    }

    return ERR_NOT_FOUND;
}

/*
 * For the given partition, see if it is at least as large as min_size.
 */
static bool check_partition_size(const gpt_partition_t* partition,
                                 const uint64_t min_size,
                                 const uint64_t block_size,
                                 const char* partition_name) {
    DEBUG_ASSERT(partition != NULL);
    DEBUG_ASSERT(partition->last >= partition->first);
    DEBUG_ASSERT(partition->name != NULL);

    uint64_t partition_size =
        block_size * (partition->last - partition->first + 1);

    if (partition_size < min_size) {
        printf("%s partition too small, found %" PRIu64 ", but require \
                %" PRIu64  "\n",
               partition_name, partition_size, min_size);
        return false;
    } else {
        return true;
    }
}

/*
 * Give GPT information, check if the table contains entries for the partitions
 * represented by part_mask. For constructing the part_mask, see the PART_*
 * definitions. This also checks the partition sizes match or exceed the defined
 * minimums. gpt_data must not be NULL and gpt_data->valid must be true.
 *
 * Returns value is a mask for missing partitions, or 0 if all partitions are
 * found and otherwise valid. The magneta EFI partition is only considered valid
 * if it is not the first partition on the device since we assume the first
 * partition of the device contains the 'native' EFI partition for the device.
 */
static partition_flags partition_for_install(gpt_device_t* gpt_data,
                                            const uint64_t block_size,
                                            partition_flags part_flags) {
    DEBUG_ASSERT(gpt_data != NULL);
    DEBUG_ASSERT(gpt_data->valid);

    if (part_flags & PART_FUCHSIA) {
        uint16_t part_id_fuchsia;
        mx_status_t rc =
            find_partition(gpt_data, guid_fuchsia_part, &part_id_fuchsia);
        gpt_partition_t* partition;
        switch (rc) {
            case ERR_NOT_FOUND:
                printf("No fuchsia partition found.\n");
                break;
            case ERR_INVALID_ARGS:
                printf("Arguments are invalid for fuchsia partition.\n");
                break;
            case ERR_BAD_STATE:
                printf("GPT descriptor is invalid.\n");
                break;
            case NO_ERROR:
                partition = gpt_data->partitions[part_id_fuchsia];
                DEBUG_ASSERT(partition->last >= partition->first);

                bool size_ok = check_partition_size(
                    partition, MIN_SIZE_FUCHSIA_PART, block_size, "Fuchsia");
                if (size_ok) {
                    // woot, things look good, remove from requested mask
                    part_flags &= ~PART_FUCHSIA;
                }
                break;
            default:
                printf("Unrecognized error finding fuchsia partition: %d\n",
                    rc);
        }
    }

    if (part_flags & PART_MAGENTA) {
        uint16_t part_id_magenta;
        mx_status_t rc =
            find_partition(gpt_data, guid_efi_part, &part_id_magenta);

        gpt_partition_t* partition;
        switch (rc) {
            case ERR_NOT_FOUND:
                printf("No magenta partition found.\n");
                break;
            case ERR_INVALID_ARGS:
                printf("Arguments are invalid for magenta partition.\n");
                break;
            case ERR_BAD_STATE:
                printf("GPT descriptor is invalid.\n");
                break;
            case NO_ERROR:
                if (part_id_magenta == 0) {
                    printf("found a magenta partition, but it is the first; ");
                    printf("assume we want to keep this one intact.\n");
                } else {
                    partition = gpt_data->partitions[part_id_magenta];
                    DEBUG_ASSERT(partition->last >= partition->first);

                    bool size_ok = check_partition_size(
                        partition, MIN_SIZE_MAGENTA_PART, block_size, "Magenta");
                    if (size_ok) {
                        // woot, things look good, remove from requested mask
                        part_flags &= ~PART_MAGENTA;
                    }
                }
                break;
            default:
                printf("Unrecognized error finding magenta parition: %d\n",
                    rc);
        }
    }
    return part_flags;
}

int main(int argc, char** argv) {
    // setup the base path in the path buffer
    static char path_buffer[PATH_MAX];
    strcpy(path_buffer, PATH_BLOCKDEVS);
    strcat(path_buffer, "/");

    const size_t base_len = strlen(path_buffer);
    DEBUG_ASSERT(base_len < PATH_MAX);
    const size_t buffer_remaining = PATH_MAX - base_len - 1;

    // first read the directory of block devices
    DIR* dir;
    dir = opendir(PATH_BLOCKDEVS);
    if (dir == NULL) {
        printf("Open failed for directory: '%s' with error %s\n",
               PATH_BLOCKDEVS, strerror(errno));
        return -1;
    }

    // device to install on
    gpt_device_t* install_dev = NULL;
    for (ssize_t rc = get_next_file_path(
             dir, buffer_remaining, &path_buffer[base_len]);
         rc >= 0;
         rc = get_next_file_path(
             dir, buffer_remaining, &path_buffer[base_len])) {
        if (rc > 0) {
            printf("Device path length overrun by %zd characters\n", rc);
            continue;
        }
        // open device read-only
        int fd = open_device_ro(&path_buffer[0]);
        if (fd < 0) {
            continue;
        }

        uint64_t block_size;
        install_dev = read_gpt(fd, &block_size);
        close(fd);

        // if we read a GPT, see if it has the entry we want
        if (install_dev != NULL && install_dev->valid) {
            int ready_for_install = partition_for_install(
                install_dev, block_size, PART_FUCHSIA | PART_MAGENTA);

            // if ready_for_install == 0, then we'd do something!

            printf("Ready for install on %s? 0x%x\n", path_buffer,
                   ready_for_install);
            if (ready_for_install != 0) {
                gpt_device_release(install_dev);
            } else {
                break;
            }
        }
    }

    closedir(dir);
    if (install_dev != NULL && install_dev->valid) {
        // do install
        gpt_device_release(install_dev);
    }
}
