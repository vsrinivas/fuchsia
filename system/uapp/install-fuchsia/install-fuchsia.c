// Copyright 2016 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fs-management/mount.h>
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
#include <time.h>
#include <unistd.h>
#include <lz4/lz4frame.h>
#include <lz4/lz4.h>

#define DEFAULT_BLOCKDEV "/dev/class/block/000"
#define PATH_BLOCKDEVS "/dev/class/block"

#define CHECK_BIT(var, pos) ((var) & (1 << (pos)))

#define PATH_VOLUMES "/volume"

// 4GB
#define MIN_SIZE_SYSTEM_PART (((uint64_t)1024u) * 1024u * 1024u * 4u)
// 1GB
#define MIN_SIZE_EFI_PART (((uint64_t)1024u) * 1024u * 1024u)

#define PATH_MAX 4096

#define NUM_INSTALL_PARTS 2

#define BLOCK_SIZE 65536

// TODO(jmatt): it is gross that we're hard-coding this here, we should take
// from the user or somehow set in the environment
#define IMG_SYSTEM_PATH "/system/installer/user_fs.lz4"
#define IMG_EFI_PATH "/system/installer/efi_fs.lz4"

// use for the partition mask sent to parition_for_install
typedef enum {
    PART_EFI = 1 << 0,
    PART_SYSTEM = 1 << 1
} partition_flags;

static const uint8_t guid_system_part[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
static const uint8_t guid_efi_part[GPT_GUID_LEN] = GUID_EFI_VALUE;

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
        fprintf(stderr, "Could not read device at %s, open reported error:%s\n",
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
        fprintf(stderr, "error getting block size, ioctl result code: %zd\n",
                rc);
        return NULL;
    }

    if (*blocksize_out < 1) {
        fprintf(stderr, "Device reports block size of %" PRIu64 ", abort!\n",
               *blocksize_out);
        return NULL;
    }

    uint64_t blocks;
    rc = ioctl_block_get_size(fd, &blocks);
    if (rc < 0) {
        fprintf(stderr, "error getting device size, ioctl result code: %zd\n",
                rc);
        return NULL;
    }

    blocks /= *blocksize_out;
    gpt_device_t* gpt;
    rc = gpt_device_init(fd, *blocksize_out, blocks, &gpt);
    if (rc < 0) {
        fprintf(stderr, "error reading GPT, result code: %zd \n", rc);
        return NULL;
    } else if (!gpt->valid) {
        fprintf(stderr, "error reading GPT, libgpt reports data is invalid\n");
        return NULL;
    }

    return gpt;
}

/*
 * Given a pointer to an array of gpt_partition_t structs, look for a partition
 * with the matching type GUID. The table_size argument tells this function how
 * many entries there are in the array.
 * Returns
 *   * ERR_INVALID_ARGS if any pointers are null
 *   * ERR_BAD_STATE if the GPT data reports itself as invalid
 *   * ERR_NOT_FOUND if the requested partition is not present
 *   * NO_ERROR if the partition is found, in which case the index is assigned
 *     to part_id_out
 */
static mx_status_t find_partition_entries(gpt_partition_t** gpt_table,
                                          const uint8_t* guid,
                                          uint16_t table_size,
                                          uint16_t* part_id_out) {
    DEBUG_ASSERT(gpt_table != NULL);
    DEBUG_ASSERT(guid != NULL);
    DEBUG_ASSERT(part_id_out != NULL);

    for (uint16_t idx = 0; idx < table_size && gpt_table[idx] != NULL; idx++) {
        uint8_t* type_ptr = gpt_table[idx]->type;
        if (!memcmp(type_ptr, guid, 16)) {
            *part_id_out = idx;
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

    uint64_t block_count = partition->last - partition->first + 1;
    uint64_t partition_size =
        block_size * (partition->last - partition->first + 1);
    printf("%s has %" PRIu64 " blocks and block size of %" PRIu64 "\n",
           partition_name, block_count, block_size);

    if (partition_size < min_size) {
        fprintf(stderr, "%s partition too small, found %" PRIu64 ", but require \
                %" PRIu64  "\n",
               partition_name, partition_size, min_size);
        return false;
    } else {
        return true;
    }
}

/*
 * Search the path at search_dir for partitions whose ID (NOT type) GUIDs
 * match the ID GUIDs in the gpt_partition_t array pointed to by part_info.
 * num_parts should match both the length of the part_info array and the
 * part_out char* array. If the call reports NO_ERROR, path_out will contain
 * an array of character pointers to paths to the partitions, these paths will
 * be relative to the directory represented by search_dir. The path_out array
 * is ordered the same as the part_info array. If some partitions are not found
 * their entries will contain just a null terminator. An error will be returned
 * if we encounter an error looking through the partition information.
 */
static mx_status_t find_partition_path(gpt_partition_t* const* part_info,
                                       char** path_out, DIR* search_dir,
                                       int num_parts) {
    if (num_parts == 0) {
        printf("No partitions requested.\n");
        return NO_ERROR;
    }
    int found_parts = 0;
    int dir_fd = dirfd(search_dir);
    if (dir_fd < 0) {
        fprintf(stderr, "Could not get descriptor for directory, '%s'.\n",
               strerror(errno));
        return ERR_IO;
    }

    // initialize the path output so we can check this sentinel value later
    for (int idx = 0; idx < num_parts; idx++) {
        if (path_out[idx] != NULL) {
            // this makes a 0-length string
            path_out[idx][0] = '\0';
        }
    }

    struct dirent* entry;
    while ((entry = readdir(search_dir)) != NULL) {
        // get a file descriptor for the entry
        int file_fd = openat(dir_fd, entry->d_name, O_RDONLY);
        if (file_fd < 0) {
            fprintf(stderr, "Error opening descriptor for %s, error:'%s'\n",
                   entry->d_name, strerror(errno));
            continue;
        }

        uint8_t partition_guid[16];
        ssize_t rc = ioctl_block_get_partition_guid(file_fd, partition_guid, 16);

        if (rc >= 0) {
            for (int idx = 0; idx < num_parts; idx++) {
                gpt_partition_t* part_targ = part_info[idx];
                char* path_targ = path_out[idx];
                if (part_targ == NULL || path_targ == NULL) {
                    continue;
                }
                if (!memcmp(partition_guid, part_targ->guid, 16)) {
                    if (strlen(path_targ) == 0) {
                        strcpy(path_targ, entry->d_name);
                        found_parts++;
                    } else {
                        fprintf(stderr, "Error, non-unique partition GUIDs!!\n");
                        close(file_fd);
                        return ERR_NOT_FOUND;
                    }
                }
            }
        } else {
            fprintf(stderr, "ioctl failed getting GUID for %s, error:(%zi) \
                    '%s'\n", entry->d_name, rc, strerror(errno));
        }

        close(file_fd);
    }

    if (found_parts != num_parts) {
        // this isn't an error per se, everything worked but we didn't find all
        // the requested pieces.
        printf("Some partitions were not found.\n");
    }

    return NO_ERROR;
}

/*
 * Given an array of GPT partition entries in gpt_table and a partition guid in
 * part_guid, validate that the partition is in the array. Further, validate
 * that the entry reports that the number of blocks in the partition multiplied
 * by the provided block_size meets the min_size. If more than one partition in
 * the array passes this test, the first match will be provided. The length of
 * the partition information array should be passed in table_size.
 *
 * If NO_ERROR is returned, part_index_out will contain the index in the GPT for
 * the requested partition. Additionally, the pointer at *part_info_out will
 * point at the gpt_partition_t with information for the requested partition.
 */
static mx_status_t find_partition(gpt_partition_t** gpt_table,
                                  const uint8_t* part_guid, uint64_t min_size,
                                  uint64_t block_size, const char* part_name,
                                  uint16_t table_size, uint16_t* part_index_out,
                                  gpt_partition_t** part_info_out) {
    // if we find a partition, but it is the wrong size, we want to keep
    // looking down the table for something that might match, this offset
    // tracks our overall progress in the table
    uint16_t part_offset = 0;

    mx_status_t rc = NO_ERROR;
    while (rc == NO_ERROR) {
        // reduce table_size by the number of entries we've examined
        rc = find_partition_entries(gpt_table, part_guid,
                                    table_size - part_offset, part_index_out);

        gpt_partition_t* partition;
        switch (rc) {
            case ERR_NOT_FOUND:
                fprintf(stderr, "No %s partition found.\n", part_name);
                break;
            case ERR_INVALID_ARGS:
                fprintf(stderr, "Arguments are invalid for %s partition.\n",
                        part_name);
                break;
            case ERR_BAD_STATE:
                printf("GPT descriptor is invalid.\n");
                break;
            case NO_ERROR:
                partition = gpt_table[*part_index_out];
                DEBUG_ASSERT(partition->last >= partition->first);

                if (check_partition_size(partition, min_size, block_size,
                                         part_name)) {
                    *part_info_out = partition;
                    // adjust the output index by part_offset
                    *part_index_out = part_offset + *part_index_out;
                    return NO_ERROR;
                } else {
                    // if the size doesn't check out, keep looking for
                    // partitions later in the table
                    uint16_t jump_size = *part_index_out + 1;

                    // advance the pointer
                    gpt_table += jump_size;
                    part_offset += jump_size;
                }
                break;
            default:
                fprintf(stderr, "Unrecognized error finding efi parition: %d\n",
                        rc);
                break;
        }
    }

    // we didn't find a suitable partition
    return ERR_NOT_FOUND;
}

/*
 * Give GPT information, check if the table contains entries for the partitions
 * represented by part_mask. For constructing the part_mask, see the PART_*
 * definitions. This also checks the partition sizes match or exceed the defined
 * minimums. gpt_data must not be NULL and gpt_data->valid must be true.
 *
 * Returns value is a mask for missing partitions, or 0 if all partitions are
 * found and otherwise valid. Upon return the part_paths_out array will contain
 * absolute paths to the partitions to use for install. The part_paths_out array
 * should be the same length as the number of partitions as represented in
 * partition_flags. The order of the paths will be in ascending order of the
 * flag value used to request that partition, so if you're looking for the
 * system and efi partitions, the efi partition will be first and then
 * system.
 *
 * The EFI partition is only considered valid if it is not the first
 * partition on the device since we assume the first partition of the device
 * contains the 'native' EFI partition for the device.
 */
static partition_flags partition_for_install(gpt_device_t* gpt_data,
                                             const uint64_t block_size,
                                             partition_flags part_flags,
                                             size_t max_path_len,
                                             char* part_paths_out[]) {
    DEBUG_ASSERT(gpt_data != NULL);
    DEBUG_ASSERT(gpt_data->valid);
    DEBUG_ASSERT(NUM_INSTALL_PARTS == 2);

    if (!gpt_data->valid) {
        return part_flags;
    }

    DIR* block_dir = NULL;
    gpt_partition_t* part_info[NUM_INSTALL_PARTS] = {NULL, NULL};
    uint8_t parts_found = 0;
    int part_masks[NUM_INSTALL_PARTS] = {0, 0};
    uint8_t parts_requested = 0;

    uint16_t part_id = 0;
    if (part_flags & PART_EFI) {

        // look for a match until we exhaust partitions
        mx_status_t rc = NO_ERROR;
        while (part_info[parts_requested] == NULL && rc == NO_ERROR) {
            uint16_t part_limit = countof(gpt_data->partitions) - part_id;
            rc = find_partition((gpt_partition_t**) &gpt_data->partitions[part_id],
                                guid_efi_part, MIN_SIZE_EFI_PART, block_size,
                                "EFI", part_limit, &part_id,
                                &part_info[parts_requested]);

            if (rc == NO_ERROR) {
                if (part_id > 0) {
                    part_masks[parts_requested] = PART_EFI;
                    parts_found++;
                } else {
                    printf("found an EFI partition, but it is the first; ");
                    printf("assume we want to keep this one intact.\n");
                    // reset part info
                    part_info[parts_requested] = NULL;
                    part_id++;
                }
            }
        }

        parts_requested++;
    }

    if (part_flags & PART_SYSTEM) {
        mx_status_t rc = find_partition((gpt_partition_t**) &gpt_data->partitions,
                                        guid_system_part, MIN_SIZE_SYSTEM_PART,
                                        block_size, "System",
                                        countof(gpt_data->partitions), &part_id,
                                        &part_info[parts_requested]);
        if (rc == NO_ERROR) {
            part_masks[parts_requested] = PART_SYSTEM;
            parts_found++;
        }
        parts_requested++;
    }

    if (parts_found == 0) {
        return part_flags;
    }

    block_dir = opendir(PATH_BLOCKDEVS);
    if (block_dir != NULL) {
        mx_status_t rc = find_partition_path(part_info, part_paths_out,
                                             block_dir, parts_requested);
        if (rc == NO_ERROR) {
            size_t base_len = strlen(PATH_BLOCKDEVS);
            for (int idx = 0; idx < parts_requested; idx++) {
                char* str_targ = part_paths_out[idx];
                // we didn't find this partition
                if (part_masks[idx] == 0) {
                    str_targ[0] = '\0';
                    continue;
                }

                // construct paths for partitions
                if (strlen(str_targ) + base_len + 2 > max_path_len) {
                    printf("Path %s/%s does not fit in provided buffer.\n",
                           PATH_BLOCKDEVS, str_targ);
                    continue;
                }
                memmove(&str_targ[base_len + 1], str_targ,
                        strlen(str_targ) + 1);
                memcpy(str_targ, PATH_BLOCKDEVS, base_len);
                memcpy(&str_targ[base_len], "/", 1);
                part_flags &= ~part_masks[idx];
            }
        }
        closedir(block_dir);
    } else {
        fprintf(stderr, "Failure reading directory %s, error: %s\n",
               PATH_BLOCKDEVS, strerror(errno));
    }

    return part_flags;
}

/*
 * Attempt to unmount all known mount paths.
 */
static mx_status_t unmount_all(void) {
    const char* static_paths[2] = {"/data", "/system"};
    for (uint16_t idx = 0; idx < countof(static_paths); idx++) {
        printf("Unmounting filesystem at %s...", static_paths[idx]);
        mx_status_t rc = umount(static_paths[idx]);
        if (rc != NO_ERROR && rc != ERR_NOT_FOUND) {
            printf("FAILURE\n");
            return ERR_BAD_STATE;
        }
        printf("SUCCESS\n");
    }

    char path[PATH_MAX];
    DIR* vols = opendir(PATH_VOLUMES);
    if (vols == NULL) {
        fprintf(stderr, "Couldn't open volumes directory for reading!\n");
        return ERR_IO;
    }

    mx_status_t result = NO_ERROR;
    struct dirent* entry = NULL;
    strcpy(path, PATH_VOLUMES);
    while ((entry = readdir(vols)) != NULL) {
        strcat(path, "/");
        int path_len = strlen(path);

        strncpy(path + path_len, entry->d_name, PATH_MAX - path_len);
        printf("Unmounting filesystem at %s...", path);
        result = umount(path);
        if (result != NO_ERROR) {
            printf("FAILURE\n");
            break;
        }
        printf("SUCCESS\n");
    }

    closedir(vols);
    return result;
}

static mx_status_t write_partition(int src, int dest, size_t* bytes_copied) {
    uint8_t read_buffer[BLOCK_SIZE];
    uint8_t decomp_buffer[BLOCK_SIZE];
    *bytes_copied = 0;

    LZ4F_decompressionContext_t dc_context;
    LZ4F_errorCode_t err = LZ4F_createDecompressionContext(&dc_context,
                                                           LZ4F_VERSION);
    if (LZ4F_isError(err)) {
        printf("Error creating decompression context: %s\n",
               LZ4F_getErrorName(err));
        return ERR_INTERNAL;
    }
    // we set special initial read parameters so we can read just the header
    // of the first frame to provide hints about how to proceed
    size_t to_read = 4;
    size_t to_expand = BLOCK_SIZE;
    ssize_t to_consume;
    size_t MB_10s = 0;
    const uint32_t divisor = 1024 * 1024 * 10;

    while ((to_consume = read(src, read_buffer, to_read)) > 0) {
        ssize_t consumed_count = 0;
        size_t chunk_size = 0;

        if (*bytes_copied > 0) {
            size_t new_val = *bytes_copied / divisor;
            if (new_val != MB_10s) {
                printf("   %zd0MB written.\r", new_val);
                fflush(stdout);
                MB_10s = new_val;
            }
        }

        while (consumed_count < to_consume) {
            to_expand = BLOCK_SIZE;
            size_t req_size = to_consume - consumed_count;
            chunk_size = LZ4F_decompress(dc_context, decomp_buffer, &to_expand,
                                         read_buffer + consumed_count, &req_size,
                                         NULL);
            if (to_expand > 0) {
                ssize_t written = write(dest, decomp_buffer, to_expand);
                if (written != (ssize_t) to_expand) {
                    printf("Error writing to partition, it may be corrupt. %s\n",
                           strerror(errno));
                    LZ4F_freeDecompressionContext(dc_context);
                    return ERR_IO;
                }
                *bytes_copied += written;
            }

            consumed_count += req_size;
        }


        // set the next read request size
        if (chunk_size > BLOCK_SIZE) {
            to_read = BLOCK_SIZE;
        } else {
            to_read = chunk_size;
        }
    }
    LZ4F_freeDecompressionContext(dc_context);

    // go to the next line so we don't overwrite the last data size print out
    printf("\n");
    if (to_consume < 0) {
        printf("Error decompressing file: %s.\n", strerror(errno));
        return ERR_IO;
    }
    return NO_ERROR;
}

int main(int argc, char** argv) {
    DEBUG_ASSERT(NUM_INSTALL_PARTS == 2);
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

    // set up structures for hold source and destination paths for partition
    // data
    char system_path[PATH_MAX];
    char efi_path[PATH_MAX];
    char* part_paths[NUM_INSTALL_PARTS] = {efi_path, system_path};
    char system_img_path[PATH_MAX] = IMG_SYSTEM_PATH;
    char efi_img_path[PATH_MAX] = IMG_EFI_PATH;
    char* disk_img_paths[NUM_INSTALL_PARTS] = {efi_img_path, system_img_path};

    // device to install on
    gpt_device_t* install_dev = NULL;
    uint64_t block_size;
    partition_flags ready_for_install = 0;
    // TODO(jmatt): switch this to user-directed partition specification or
    // add in the EFI partition when things are ready
    partition_flags requested_parts = PART_EFI | PART_SYSTEM;
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

        install_dev = read_gpt(fd, &block_size);
        close(fd);

        // if we read a GPT, see if it has the entry we want
        if (install_dev != NULL && install_dev->valid) {
            ready_for_install = partition_for_install(
                install_dev, block_size, requested_parts,
                PATH_MAX, part_paths);

            // if ready_for_install == 0, then we'd do something!

            printf("Ready for install on %s? 0x%x\n", path_buffer,
                   ready_for_install);
            if (ready_for_install != 0) {
                gpt_device_release(install_dev);
                install_dev = NULL;
            } else {
                break;
            }
        }
    }

    closedir(dir);
    if (install_dev != NULL && install_dev->valid) {
        if (unmount_all() != NO_ERROR) {
            gpt_device_release(install_dev);
            return -1;
        }

        uint8_t part_idx = -1;
        // scan through the requested partitions bitmask to see which
        // partitions we want to write to and find the corresponding path for
        // the disk image for that partition
        for (int idx = 0; idx < 32; idx++) {
            // see if this was a requested part, if so move the index we use to
            // access the part_paths array because that array is ordered based
            // on index of the order of bits in the bitmask sent to
            // partition_for_install
            if (CHECK_BIT(requested_parts, idx)) {
                part_idx++;
            }

            // if either we weren't interested or we were, but we didn't find
            // the partition, skip
            if (!CHECK_BIT(requested_parts, idx) ||
                (CHECK_BIT(requested_parts, idx) &&
                 CHECK_BIT(ready_for_install, idx))) {
                continue;
            }

            // do install
            size_t bytes_written;
            int fd_dst = open(part_paths[part_idx], O_RDWR);
            if (fd_dst == -1) {
                printf("Error opening output device, %s\n", strerror(errno));
                gpt_device_release(install_dev);
                return -1;
            }

            int fd_src = open(disk_img_paths[idx], O_RDONLY);
            if (fd_src == -1) {
                printf("Error opening disk image file, %s\n", strerror(errno));
                close(fd_dst);
                gpt_device_release(install_dev);
                return -1;
            }

            time_t start;
            time(&start);
            mx_status_t rc = write_partition(fd_src, fd_dst, &bytes_written);
            time_t end;
            time(&end);

            printf("%.f secs taken to write %zd bytes\n", difftime(end, start),
                   bytes_written);
            close(fd_dst);
            close(fd_src);

            if (rc != NO_ERROR) {
                gpt_device_release(install_dev);
                printf("Error %i writing partition\n", rc);
                return -1;
            }
        }

        // do install
        gpt_device_release(install_dev);
        return 0;
    } else {
        // no device looks configured the way we want for install, see if we can
        // partition a device and make it suitable
        return -1;
    }
}
