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

#include <installer/lib-installer.h>

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

// the first and last 17K of the disk using GPT is reserved for
// 512B for the MBR, 512B the GPT header, and 16K for for 128, 128B
// partition entries. Technically the reserved space is two blocks
// plus 16KB, so we're making an assumption here that block sizes are
// 512B.
#define SIZE_RESERVED (((uint32_t) 17) * 1024)

// use for the partition mask sent to parition_for_install
typedef enum {
    PART_EFI = 1 << 0,
    PART_SYSTEM = 1 << 1
} partition_flags;

typedef struct part_tuple {
    int index;
    size_t first;
} part_tuple_t;

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

static int compare(const void* ls, const void* rs) {
    return (int) (((part_tuple_t*) ls)->first - ((part_tuple_t*) rs)->first);
}

/*
 * Sort an array of gpt_partition_t pointers based on the values of
 * gpt_partition_t->first. The returned value will contain an array of pointers
 * to partitions in sorted order. This array was allocated on the heap and
 * should be freed at some point.
 */
static gpt_partition_t** sort_partitions(gpt_partition_t** parts,
                                         uint16_t count) {
    gpt_partition_t** sorted_parts = malloc(count * sizeof(gpt_partition_t*));
    if (sorted_parts == NULL) {
        fprintf(stderr, "Unable to sort partitions, out of memory.\n");
        return NULL;
    }

    part_tuple_t* sort_tuples = malloc(count * sizeof(part_tuple_t));
    if (sort_tuples == NULL) {
        fprintf(stderr, "Unable to sort partitions, out of memory.\n");
        free(sorted_parts);
        return NULL;
    }

    for (uint16_t idx = 0; idx < count; idx++, sort_tuples++) {
        sort_tuples->index = idx;
        sort_tuples->first = parts[idx]->first;
    }

    sort_tuples -= count;
    qsort(sort_tuples, count, sizeof(part_tuple_t), compare);

    // create a sorted array of pointers
    for (uint16_t idx = 0; idx < count; idx++, sort_tuples++) {
        sorted_parts[idx] = parts[sort_tuples->index];
    }

    sort_tuples -= count;
    free(sort_tuples);
    return sorted_parts;
}

/*
 * Attempt to find an unallocated portion of the specified device that is at
 * least blocks_req in size. block_count should contain the total number of
 * blocks on the disk. The offset in blocks of the allocation hole will be
 * returned or 0 if no suitable space is located. 0 is used as a sentinel here
 * because space would never be available at block 0 because the first blocks
 * of the disk are used for the GPT and/or MBR.
 */
static size_t find_available_space(gpt_device_t* device, size_t blocks_req,
                                   size_t block_count, size_t block_size) {
    gpt_partition_t** sorted_parts;
    // 17K is reserved at the front and back of the disk for the protected MBR
    // and the GPT. The front is the primary of these and the back is the backup
    const uint32_t blocks_resrvd = SIZE_RESERVED / block_size;

    // if the device has no partitions, we can add one after the reserved
    // section at the front
    if (device->partitions[0] == NULL) {
        if (block_count - blocks_resrvd * 2 >= blocks_req) {
            // TODO link to GPT constant
            return blocks_resrvd;
        } else {
            return 0;
        }
    }

    // check if the GPT is sorted by partition position
    bool sorted = true;
    size_t count = 0;
    for (count = 1; count < PARTITIONS_COUNT &&
             device->partitions[count] != NULL && sorted; count++) {
        sorted = device->partitions[count - 1]->first <
            device->partitions[count]->first;
    }

    if (!sorted) {
        // count the number of valid partitions because in this case we would
        // have bailed out of counting early
        for (count = 0; device->partitions[count] != NULL; count++);
        sorted_parts = sort_partitions(device->partitions, count);
        if (sorted_parts == NULL) {
            return 0;
        }
    } else {
        sorted_parts = device->partitions;
    }

    // check to see if we have space at the beginning of the disk
    if (sorted_parts[0]->first - blocks_resrvd >= blocks_req) {
        if (!sorted) {
            free(sorted_parts);
        }
        return blocks_resrvd;
    }

    // check if there is space between partitions
    for (size_t idx = 1; idx < count; idx++) {
        if (sorted_parts[idx]->first - sorted_parts[idx - 1]->last - 1 >=
            blocks_req) {
            size_t offset = sorted_parts[idx - 1]->last + 1;
            if (!sorted) {
                free(sorted_parts);
            }
            return offset;
        }
    }

    // check to see if there is space at the end of the disk
    if (block_count - sorted_parts[count - 1]->last - 1 - blocks_resrvd >=
        blocks_req) {
        size_t offset = sorted_parts[count - 1]->last + 1;
        if (!sorted) {
            free(sorted_parts);
        }
        return offset;
    }

    if (!sorted) {
        free(sorted_parts);
    }

    return 0;
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
static partition_flags find_install_partitions(gpt_device_t* gpt_data,
                                               const uint64_t block_size,
                                               partition_flags part_flags,
                                               size_t max_path_len,
                                               char* part_paths_out[]) {
    DEBUG_ASSERT(gpt_data != NULL);
    DEBUG_ASSERT(gpt_data->valid);
    static_assert(NUM_INSTALL_PARTS == 2,
                  "Install partition count is unexpected, expected 2.");

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
                // check if this is the first partition on disk, we could sort
                // but that seems overly involved for our simple requirements
                // for this use case
                bool first = true;
                for (int idx = 0;
                     idx < PARTITIONS_COUNT && gpt_data->partitions[idx] != NULL;
                     idx++) {
                    if (gpt_data->partitions[part_id]->first >
                        gpt_data->partitions[idx]->first) {
                        first = false;
                        break;
                    }
                }

                if (!first) {
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
                    fprintf(stderr,
                            "Path %s/%s does not fit in provided buffer.\n",
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
    mx_status_t result = NO_ERROR;
    for (uint16_t idx = 0; idx < countof(static_paths); idx++) {
        printf("Unmounting filesystem at %s...", static_paths[idx]);
        mx_status_t rc = umount(static_paths[idx]);
        if (rc != NO_ERROR && rc != ERR_NOT_FOUND) {
            // why not return failure? we're just making a best effort attempt,
            // the system can return an error from this unmount call
            printf("FAILURE\n");
            result = rc;
        } else {
            printf("SUCCESS\n");
        }
    }

    char path[PATH_MAX];
    DIR* vols = opendir(PATH_VOLUMES);
    if (vols == NULL) {
        fprintf(stderr, "Couldn't open volumes directory for reading!\n");
        return ERR_IO;
    }

    struct dirent* entry = NULL;
    strcpy(path, PATH_VOLUMES);
    strcat(path, "/");
    int path_len = strlen(path);

    while ((entry = readdir(vols)) != NULL) {
        strncpy(path + path_len, entry->d_name, PATH_MAX - path_len);
        printf("Unmounting filesystem at %s...", path);
        result = umount(path);
        if (result != NO_ERROR) {
            printf("FAILURE\n");
        } else {
            printf("SUCCESS\n");
        }
    }

    closedir(vols);
    // take a power nap, the system may take a moment to free resources after
    // unmounting
    sleep(1);
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
                    fprintf(stderr,
                            "Error writing to partition, it may be corrupt. %s\n",
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
        fprintf(stderr, "Error decompressing file: %s.\n", strerror(errno));
        return ERR_IO;
    }
    return NO_ERROR;
}

static bool do_sort_test(int test_size, uint64_t val_max) {
    gpt_partition_t* values = malloc(test_size * sizeof(gpt_partition_t));
    gpt_partition_t** value_ptrs = malloc(test_size * sizeof(gpt_partition_t*));
    if (values == NULL) {
        fprintf(stderr, "Unable to allocate memory for test\n");
        return false;
    }

    for (int idx = 0; idx < test_size; idx++) {
        bool unique = false;
        while (!unique) {
            double rando = ((double) rand()) / RAND_MAX;
            uint64_t val = rando * val_max;

            (values + idx)->first = val;

            // check the uniqueness of the value since our sort doesn't handle
            // duplicate values
            unique = true;
            for (int idx2 = 0; idx2 < idx; idx2++) {
                if ((values + idx2)->first == val) {
                    unique = false;
                    break;
                }
            }

            value_ptrs[idx] = values + idx;
        }
    }

    gpt_partition_t** sorted_values = sort_partitions(value_ptrs, test_size);

    // check that we're strictly in ascending order
    bool ordered = true;
    for (int idx = 1; idx < test_size; idx++) {
        if (sorted_values[idx - 1]->first > sorted_values[idx]->first) {
            ordered = false;
            printf("Values are not ordered, index: %i--%" PRIu64 "!\n", idx,
                   sorted_values[idx]->first);
        }
    }

    if (!ordered) {
        for (int idx = 0; idx < test_size; idx++) {
            printf(" -- %" PRIu64 "", sorted_values[idx]->first);
        }
        printf("\n");
    }


    free(values);
    free(sorted_values);
    free(value_ptrs);
    return ordered;
}

static bool test_sort(void) {
    // run 20 iterations with 20K elements as a stress test. We also think
    // this should hit all possible code paths
    for (int count = 0; count < 20; count++) {
        if (!do_sort_test(20000, 10000000)) {
            return false;
        }
        printf(".");
        fflush(stdout);
    }

    printf("\nAll tests completed.\n");
    return true;
}

mx_status_t add_partition(gpt_device_t* device, uint64_t offset_blocks,
                          uint64_t size_blocks, uint8_t* guid_type,
                          const char* name) {
    uint8_t guid_id[GPT_GUID_LEN];
    size_t rand_size = 0;
    mx_status_t rc = mx_cprng_draw(guid_id, GPT_GUID_LEN, &rand_size);
    if (rc != NO_ERROR || rand_size != GPT_GUID_LEN) {
        fprintf(stderr, "Sys call failed to set all random bytes, err: %s\n",
                strerror(errno));
        return rc;
    }

    int gpt_result = gpt_partition_add(device, name, guid_type, guid_id,
                                       offset_blocks, size_blocks, 0);
    if (gpt_result < 0) {
        fprintf(stderr, "Error adding partition code: %i\n", gpt_result);
        return ERR_INTERNAL;
    }

    gpt_result = gpt_device_sync(device);
    if (gpt_result < 0) {
        fprintf(stderr, "Error writing partition table, code: %i\n", gpt_result);
        return ERR_IO;
    }

    return NO_ERROR;
}

/*
 * Take a directory stream of devices, the path to that directory, and a bit
 * mask describing which partitions are being looked for and determines which
 * partitions are available, what their device path is, and loads the
 * gpt_device_t struct for the device containing the partition(s). The
 * unfound_parts_out bit mask has bits set for any partitions not found. The
 * part_paths_out array is indexed by the position of the corresponding bit
 * in the requested_parts bit mask and size of the array passed in should
 * match the number of partitions requested. If the requested_parts bit mask
 * is 0b1011 the array should be length three. If the unfound_parts_out mask
 * were set to 0b0010 then the path array would be null at index 1 while index
 * 0 and 2 would point to a string.
 */
mx_status_t find_install_device(DIR* dir, const char* dir_path,
                                partition_flags requested_parts,
                                partition_flags* unfound_parts_out,
                                char* part_paths_out[],
                                gpt_device_t** device_out) {
    char path_buffer[PATH_MAX];
    strcpy(path_buffer, dir_path);
    const size_t base_len = strlen(path_buffer);
    const size_t buffer_remaining = PATH_MAX - base_len - 1;
    uint64_t block_size;
    gpt_device_t* install_dev = NULL;

    for (ssize_t rc = get_next_file_path(
             dir, buffer_remaining, &path_buffer[base_len]);
         rc >= 0;
         rc = get_next_file_path(
             dir, buffer_remaining, &path_buffer[base_len])) {
        if (rc > 0) {
            fprintf(stderr,
                    "Device path length overrun by %zd characters\n", rc);
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
            *unfound_parts_out = find_install_partitions(
                install_dev, block_size, requested_parts,
                PATH_MAX, part_paths_out);

            printf("Ready for install on %s? 0x%x\n", path_buffer,
                   *unfound_parts_out);
            if (*unfound_parts_out != 0) {
                gpt_device_release(install_dev);
                install_dev = NULL;
            } else {
                *device_out = install_dev;
                break;
            }
        }
    }

    if (install_dev != NULL) {
        return NO_ERROR;
    } else {
        return ERR_NOT_FOUND;
    }
}

/*
 * Write out the install data from the source paths into the destination
 * paths. A partition is only written if its bit is set in both parts_requested
 * and parts_available masks. The paths_src array should be indexed based the
 * position of the bit in the masks while the paths_dest array should be
 * indexed based on how many requested partitions there are.
 */
mx_status_t write_install_data(partition_flags parts_requested,
                               partition_flags parts_available,
                               char* paths_src[],
                               char* paths_dest[]) {
    if (unmount_all() != NO_ERROR) {
        // this isn't necessarily a failure, some of the paths that we tried
        // to unmount not exist or might not actually correspond to devices
        // we want to write to. We'll try to open the devices we want to
        // write to and see what happens
        printf("Warning, devices might not be unmounted.\n");
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
        if (CHECK_BIT(parts_requested, idx)) {
            part_idx++;
        }

        // if either we weren't interested or we were, but we didn't find
        // the partition, skip
        if (!CHECK_BIT(parts_requested, idx) ||
            (CHECK_BIT(parts_requested, idx) &&
             CHECK_BIT(parts_available, idx))) {
            continue;
        }

        // do install
        size_t bytes_written;
        int fd_dst = open(paths_dest[part_idx], O_RDWR);
        if (fd_dst == -1) {
            fprintf(stderr, "Error opening output device, %s\n",
                    strerror(errno));
            return ERR_IO;
        }

        int fd_src = open(paths_src[idx], O_RDONLY);
        if (fd_src == -1) {
            fprintf(stderr, "Error opening disk image file, %s\n",
                    strerror(errno));
            close(fd_dst);
            return ERR_IO;
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
            fprintf(stderr, "Error %i writing partition\n", rc);
            return rc;
        }
    }

    return NO_ERROR;
}

/*
 * Given a directory, assume its contents represent block devices. Look at
 * each entry to see if it contains a GPT and if it does, see if the GPT
 * reports that space_required contiguous bytes are available. If a suitable
 * place is found device_path_out and offset_out will be set to valid values,
 * otherwise they will be left unchanged.
 */
void find_device_with_space(DIR* dir, char* dir_path, uint64_t space_required,
                            char* device_path_out, size_t* offset_out) {
    char path_buffer[PATH_MAX];
    strcpy(path_buffer, dir_path);
    size_t base_len = strlen(path_buffer);
    size_t buffer_remaining = PATH_MAX - base_len - 1;
    uint64_t block_size;

    // no device looks configured the way we want for install, see if we can
    // partition a device and make it suitable
    for (ssize_t rc = get_next_file_path(
             dir, buffer_remaining, &path_buffer[base_len]);
         rc >= 0;
         rc = get_next_file_path(
             dir, buffer_remaining, &path_buffer[base_len])) {
        if (rc > 0) {
            fprintf(stderr, "Device path length overrun by %zd characters\n",
                    rc);
            continue;
        }

        // open device read-only
        int device_fd = open_device_ro(&path_buffer[0]);
        if (device_fd < 0) {
            fprintf(stderr, "Error reading directory");
            continue;
        }

        uint64_t disk_size;
        ssize_t rc2 = ioctl_block_get_size(device_fd, &disk_size);
        ssize_t rc3 = ioctl_block_get_blocksize(device_fd, &block_size);
        if (rc2 < 0 || rc3 < 0) {
            fprintf(stderr, "Unable to read disk or block size.\n");
            close(device_fd);
            return;
        }

        gpt_device_t* install_dev = read_gpt(device_fd, &block_size);

        if (install_dev == NULL) {
            close(device_fd);
            continue;
        } else if (!install_dev->valid) {
            fprintf(stderr, "Read GPT for %s, but it is invalid\n",
                    path_buffer);
            gpt_device_release(install_dev);
            close(device_fd);
            continue;
        }

        size_t space_offset = find_available_space(
            install_dev, space_required / block_size, disk_size / block_size,
            block_size);
        gpt_device_release(install_dev);
        close(device_fd);
        if (space_offset > 0) {
            strcpy(device_path_out, path_buffer);
            *offset_out = space_offset;
            return;
        }
    }
}


/*
 * Create the system partition and ESP on the specified device, starting at the
 * specified block offset.
 */
mx_status_t create_partitions(char* dev_path, uint64_t block_offset) {
    printf("Adding partitions...\n");
    // open a read/write fd for the block device
    int rw_dev = open(dev_path, O_RDWR);
    if (rw_dev < 0) {
        fprintf(stderr, "couldn't open device read/write\n");
        return ERR_IO;
    }
    uint64_t block_size;
    gpt_device_t* gpt_edit = read_gpt(rw_dev, &block_size);

    // TODO(jmatt): consider asking the user what device to partition
    // install_dev should point to the device we want to modify
    uint64_t size_blocks = MIN_SIZE_SYSTEM_PART / block_size;
    uint8_t type_system[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
    mx_status_t rc = add_partition(gpt_edit, block_offset, size_blocks,
                                       type_system, "system");
    if (rc != NO_ERROR) {
        gpt_device_release(gpt_edit);
        close(rw_dev);
        return rc;
    }

    uint64_t size_blocks_efi = MIN_SIZE_EFI_PART / block_size;
    uint8_t type_efi[GPT_GUID_LEN] = GUID_EFI_VALUE;
    rc = add_partition(gpt_edit, block_offset + size_blocks,
                       size_blocks_efi, type_efi, "EFI");
    if (rc != NO_ERROR) {
        gpt_device_release(gpt_edit);
        close(rw_dev);
        return rc;
    }

    gpt_device_release(gpt_edit);

    // force a re-read of the block device so the new partitions are
    // properly picked up
    ioctl_block_rr_part(rw_dev);
    close(rw_dev);
    printf("Partitions added, please run installer again.\n");
    return NO_ERROR;
}

int main(int argc, char** argv) {
    static_assert(NUM_INSTALL_PARTS == 2,
                  "Install partition count is unexpected, expected 2.");
    static_assert(PATH_MAX >= sizeof(PATH_BLOCKDEVS),
                  "File path max length is too short for path to block devices.");
    // setup the base path in the path buffer
    static char path_buffer[sizeof(PATH_BLOCKDEVS) + 2];
    strcpy(path_buffer, PATH_BLOCKDEVS);
    strcat(path_buffer, "/");

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
    //uint64_t block_size;
    partition_flags ready_for_install = 0;
    partition_flags requested_parts = PART_EFI | PART_SYSTEM;

    // the dirty bit is set to true whenever the devices directory is in a
    // state where it it unknown if installation can proceed
    bool retry = false;
    do {
        // first read the directory of block devices
        DIR* dir = opendir(PATH_BLOCKDEVS);
        if (dir == NULL) {
            fprintf(stderr, "Open failed for directory: '%s' with error %s\n",
                    PATH_BLOCKDEVS, strerror(errno));
            return -1;
        }

        mx_status_t rc = find_install_device(dir, path_buffer, requested_parts,
                                             &ready_for_install, part_paths,
                                             &install_dev);
        closedir(dir);

        if (rc == NO_ERROR && install_dev->valid) {
            rc = write_install_data(requested_parts, ready_for_install,
                                    disk_img_paths, part_paths);
            gpt_device_release(install_dev);
            if (rc == NO_ERROR) {
                return 0;
            } else {
                return -1;
            }
        } else {
            dir = opendir(PATH_BLOCKDEVS);
            if (dir == NULL) {
                fprintf(stderr, "Open failed for directory: '%s' with error %s\n",
                        PATH_BLOCKDEVS, strerror(errno));
                return -1;
            }

            strcpy(path_buffer, PATH_BLOCKDEVS);
            strcat(path_buffer, "/");

            char device_path[PATH_MAX];
            size_t space_offset = 0;
            find_device_with_space(dir, path_buffer,
                                   MIN_SIZE_SYSTEM_PART + MIN_SIZE_EFI_PART,
                                   device_path, &space_offset);
            closedir(dir);
            if (space_offset == 0) {
                // TODO don't give up, try removing one or more partitions
                continue;
            }

            // if partition creation succeeds, set the dirty bit
            retry = create_partitions(device_path, space_offset) == NO_ERROR;
        }
    } while (retry);
}
