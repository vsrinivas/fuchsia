// Copyright 2016 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fs-management/mount.h>
#include <gpt/gpt.h>
#include <inttypes.h>
#include <limits.h>
#include <lz4/lz4.h>
#include <lz4/lz4frame.h>
#include <zircon/device/block.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <fdio/io.h>
#include <fdio/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <installer/installer.h>
#include <installer/sparse.h>

#define DEFAULT_BLOCKDEV "/dev/class/block/000"

#define CHECK_BIT(var, pos) ((var) & (1 << (pos)))

#define PATH_VOLUMES "/volume"

// 4GB
#define MIN_SIZE_SYSTEM_PART (((uint64_t)1024u) * 1024u * 1024u * 4u)
// 1GB
#define MIN_SIZE_EFI_PART (((uint64_t)1024u) * 1024u * 1024u)

// data must be at least 200MB
#define MIN_SIZE_DATA (((uint64_t)1024u) * 1024u * 200)

// we'd like data to be 8GB
#define PREFERRED_SIZE_DATA (((uint64_t)1024u) * 1024u * 1024u * 8u)

#define PATH_MAX 4096

#define NUM_INSTALL_PARTS 2

// The size of memory blocks to use while decompressing the LZ4 file.
// The LZ4 compressed file is expected to have 64K blocks. If the file being
// decompressed is a sparsed file the 64K block may contain a sparse file header
// and therefore the data in the decompressed section may not align to boundaries
// of the block device we're writing to. If this is true, then we need to
// keep a partial device block's worth of data and decompress a new section
// from the LZ4 file. At most we expect device blocks to be 4K and therefore this
// is the most we'd have left over
#define DECOMP_BLOCK_SIZE ((64 + 4) * 1024)

// TODO(jmatt): it is gross that we're hard-coding this here, we should take
// from the user or somehow set in the environment
#define IMG_SYSTEM_PATH "/system/installer/user_fs.lz4"
#define IMG_EFI_PATH "/system/installer/efi_fs.lz4"

// use for the partition mask sent to parition_for_install
typedef enum { PART_EFI = 1 << 0, PART_SYSTEM = 1 << 1 } partition_flags;

typedef struct disk_rec {
  list_node_t disk_node;
  gpt_device_t *device;
  char *path;
  uint16_t part_count;
} disk_rec_t;

static const uint8_t guid_system_part[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
static const uint8_t guid_efi_part[GPT_GUID_LEN] = GUID_EFI_VALUE;

static uint16_t count_partitions(gpt_device_t *device) {
  assert(device != NULL);

  if (device == NULL) {
    return 0;
  }

  uint16_t counter;
  for (counter = 0; device->partitions[counter] != NULL; counter++)
    ;
  return counter;
}

/*
 * Search the path at search_dir for partitions whose ID (NOT type) GUIDs
 * match the ID GUIDs in the gpt_partition_t array pointed to by part_info.
 * num_parts should match both the length of the part_info array and the
 * part_out char* array. If the call reports ZX_OK, path_out will contain
 * an array of character pointers to paths to the partitions, these paths will
 * be relative to the directory represented by search_dir. The path_out array
 * is ordered the same as the part_info array. If some partitions are not found
 * their entries will contain just a null terminator. An error will be returned
 * if we encounter an error looking through the partition information.
 */
static zx_status_t find_partition_path(gpt_partition_t *const *part_info,
                                       char **path_out, DIR *search_dir,
                                       int num_parts) {
  if (num_parts == 0) {
    printf("No partitions requested.\n");
    return ZX_OK;
  }
  int found_parts = 0;
  int dir_fd = dirfd(search_dir);
  if (dir_fd < 0) {
    fprintf(stderr, "Could not get descriptor for directory, '%s'.\n",
            strerror(errno));
    return ZX_ERR_IO;
  }

  // initialize the path output so we can check this sentinel value later
  for (int idx = 0; idx < num_parts; idx++) {
    if (path_out[idx] != NULL) {
      // this makes a 0-length string
      path_out[idx][0] = '\0';
    }
  }

  struct dirent *entry;
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
        gpt_partition_t *part_targ = part_info[idx];
        char *path_targ = path_out[idx];
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
            return ZX_ERR_NOT_FOUND;
          }
        }
      }
    } else {
      fprintf(stderr, "Warning: ioctl failed getting GUID for %s, error:(%zi) '%s'\n",
              entry->d_name, rc, strerror(errno));
    }

    close(file_fd);
  }

  if (found_parts != num_parts) {
    // this isn't an error per se, everything worked but we didn't find all
    // the requested pieces.
    printf("Some partitions were not found.\n");
  }

  return ZX_OK;
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
static partition_flags find_install_partitions(gpt_device_t *gpt_data,
                                               const uint64_t block_size,
                                               partition_flags part_flags,
                                               size_t max_path_len,
                                               char *part_paths_out[]) {
  assert(gpt_data != NULL);
  assert(gpt_data->valid);
  static_assert(NUM_INSTALL_PARTS == 2,
                "Install partition count is unexpected, expected 2.");

  if (!gpt_data->valid) {
    return part_flags;
  }

  DIR *block_dir = NULL;
  gpt_partition_t *part_info[NUM_INSTALL_PARTS] = {NULL, NULL};
  uint8_t parts_found = 0;
  int part_masks[NUM_INSTALL_PARTS] = {0, 0};
  uint8_t parts_requested = 0;

  uint16_t part_id = 0;
  if (part_flags & PART_EFI) {
    // look for a match until we exhaust partitions
    zx_status_t rc = ZX_OK;
    while (part_info[parts_requested] == NULL && rc == ZX_OK) {
      uint16_t part_limit = countof(gpt_data->partitions) - part_id;
      rc = find_partition((gpt_partition_t **)&gpt_data->partitions[part_id],
                          &guid_efi_part, MIN_SIZE_EFI_PART, block_size, "ESP",
                          part_limit, &part_id, &part_info[parts_requested]);

      if (rc == ZX_OK) {
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
    zx_status_t rc = find_partition(
        (gpt_partition_t **)&gpt_data->partitions, &guid_system_part,
        MIN_SIZE_SYSTEM_PART, block_size, "System",
        countof(gpt_data->partitions), &part_id, &part_info[parts_requested]);
    if (rc == ZX_OK) {
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
    zx_status_t rc = find_partition_path(part_info, part_paths_out, block_dir,
                                         parts_requested);
    if (rc == ZX_OK) {
      size_t base_len = strlen(PATH_BLOCKDEVS);
      for (int idx = 0; idx < parts_requested; idx++) {
        char *str_targ = part_paths_out[idx];
        // we didn't find this partition
        if (part_masks[idx] == 0) {
          str_targ[0] = '\0';
          continue;
        }

        // construct paths for partitions
        if (strlen(str_targ) + base_len + 2 > max_path_len) {
          fprintf(stderr, "Path %s/%s does not fit in provided buffer.\n",
                  PATH_BLOCKDEVS, str_targ);
          continue;
        }
        memmove(&str_targ[base_len + 1], str_targ, strlen(str_targ) + 1);
        memcpy(str_targ, PATH_BLOCKDEVS, base_len);
        memcpy(&str_targ[base_len], "/", 1);
        part_flags &= ~part_masks[idx];
      }
    }
    closedir(block_dir);
  } else {
    fprintf(stderr, "Failure reading directory %s, error: %s\n", PATH_BLOCKDEVS,
            strerror(errno));
  }

  return part_flags;
}

/*
 * Attempt to unmount all known mount paths.
 */
static zx_status_t unmount_all(void) {
  const char *static_paths[1] = {"/data"};
  zx_status_t result = ZX_OK;
  for (uint16_t idx = 0; idx < countof(static_paths); idx++) {
    zx_status_t rc = umount(static_paths[idx]);
    if (rc != ZX_OK && rc != ZX_ERR_NOT_FOUND) {
      // why not return failure? we're just making a best effort attempt,
      // the system can return an error from this unmount call
      result = rc;
      printf("Warning: Unmounting filesystem at %s failed.\n",
             static_paths[idx]);
    }
  }

  char path[PATH_MAX];
  DIR *vols = opendir(PATH_VOLUMES);
  if (vols == NULL) {
    fprintf(stderr, "Couldn't open volumes directory for reading!\n");
    return ZX_ERR_IO;
  }

  struct dirent *entry = NULL;
  strcpy(path, PATH_VOLUMES);
  strcat(path, "/");
  int path_len = strlen(path);

  while ((entry = readdir(vols)) != NULL) {
    if (!strncmp(".", entry->d_name, strlen(entry->d_name)) ||
        !strncmp("..", entry->d_name, strlen(entry->d_name))) {
      continue;
    }
    strncpy(path + path_len, entry->d_name, PATH_MAX - path_len);

    zx_status_t rc = umount(path);
    if (rc != ZX_OK) {
      printf("Warning: Unmounting filesystem at %s failed.\n", path);
    }
    if (result == ZX_OK && rc != ZX_OK) {
      result = rc;
    }
  }

  closedir(vols);
  // take a power nap, the system may take a moment to free resources after
  // unmounting
  sleep(1);
  return result;
}

static zx_status_t write_partition(int src, int dest, size_t *bytes_copied) {
  uint8_t read_buffer[DECOMP_BLOCK_SIZE];
  uint8_t decomp_buffer[DECOMP_BLOCK_SIZE];
  *bytes_copied = 0;

  LZ4F_decompressionContext_t dc_context;
  LZ4F_errorCode_t err =
      LZ4F_createDecompressionContext(&dc_context, LZ4F_VERSION);
  if (LZ4F_isError(err)) {
    printf("Error creating decompression context: %s\n",
           LZ4F_getErrorName(err));
    return ZX_ERR_INTERNAL;
  }
  // we set special initial read parameters so we can read just the header
  // of the first frame to provide hints about how to proceed
  size_t to_read = 4;
  size_t to_expand = DECOMP_BLOCK_SIZE;
  ssize_t to_consume;
  size_t MB_10s = 0;
  const uint32_t divisor = 1024 * 1024 * 10;
  unsparse_ctx_t write_ctx;
  init_unsparse_ctx(&write_ctx);

  // remainder will be the amount of data decompressed, but not written out and
  // therefore leftover in the decompression buffer
  ssize_t remainder = 0;
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
      // space available in the decompression buffer
      to_expand = DECOMP_BLOCK_SIZE - remainder;

      // bytes read from disk yet to decompressed
      size_t req_size = to_consume - consumed_count;
      chunk_size =
          LZ4F_decompress(dc_context, decomp_buffer + remainder, &to_expand,
                          read_buffer + consumed_count, &req_size, NULL);
      if (LZ4F_isError(chunk_size)) {
        fprintf(stderr, "Error decompressing volume file.\n");
        return ZX_ERR_INTERNAL;
      }

      if (to_expand > 0) {
        // newly decompressed data, plus any left in decompression buffer from
        // previous iteration
        size_t unsparse_data = to_expand + remainder;

        // unsparse the data and write it out, checking to see how much of the
        // buffer was consumed
        ssize_t written = unsparse_buf(decomp_buffer, unsparse_data,
                                       &write_ctx, dest);
        remainder = (ssize_t) unsparse_data - written;

        if (written < 0) {
          fprintf(stderr, "Error writing to partition, it may be corrupt %zi. %zu %zu %s\n", *bytes_copied, unsparse_data, remainder,
                  strerror(errno));
          LZ4F_freeDecompressionContext(dc_context);
          return ZX_ERR_IO;
        } else if ((size_t) written < unsparse_data) {
          // unsparse_buf didn't consume the whole buffer, move remaining data
          // to front of buffer
          memmove(decomp_buffer, decomp_buffer + written, remainder);
        }
        *bytes_copied += (size_t) written;
      }

      consumed_count += req_size;
    }

    // set the next read request size
    if (chunk_size > DECOMP_BLOCK_SIZE) {
      to_read = DECOMP_BLOCK_SIZE;
    } else {
      to_read = chunk_size;
    }
  }
  LZ4F_freeDecompressionContext(dc_context);

  // go to the next line so we don't overwrite the last data size print out
  printf("\n");
  if (to_consume < 0) {
    fprintf(stderr, "Error decompressing file: %s.\n", strerror(errno));
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t add_partition(gpt_device_t *device, uint64_t offset_blocks,
                          uint64_t size_blocks, uint8_t *guid_type,
                          const char *name) {
  uint8_t guid_id[GPT_GUID_LEN];
  size_t rand_size = 0;
  zx_status_t rc = zx_cprng_draw(guid_id, GPT_GUID_LEN, &rand_size);
  if (rc != ZX_OK || rand_size != GPT_GUID_LEN) {
    fprintf(stderr, "Sys call failed to set all random bytes, err: %s\n",
            strerror(errno));
    return rc;
  }

  int gpt_result = gpt_partition_add(device, name, guid_type, guid_id,
                                     offset_blocks, size_blocks, 0);
  if (gpt_result < 0) {
    fprintf(stderr, "Error adding partition code: %i\n", gpt_result);
    return ZX_ERR_INTERNAL;
  }

  gpt_result = gpt_device_sync(device);
  if (gpt_result < 0) {
    fprintf(stderr, "Error writing partition table, code: %i\n", gpt_result);
    return ZX_ERR_IO;
  }

  return ZX_OK;
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
 *
 * If successful dev_path_out will contain the path to the device that hosts
 * the found partitions. max_len should specify the size of the buffer that
 * dev_path_out points to.
 */
zx_status_t find_install_device(DIR *dir, const char *dir_path,
                                partition_flags requested_parts,
                                partition_flags *unfound_parts_out,
                                char *part_paths_out[],
                                gpt_device_t **device_out, char *dev_path_out,
                                size_t max_len) {
  strcpy(dev_path_out, dir_path);
  const size_t base_len = strlen(dev_path_out);
  const size_t buffer_remaining = max_len - base_len - 1;
  uint64_t block_size;
  gpt_device_t *install_dev = NULL;

  for (ssize_t rc =
           get_next_file_path(dir, buffer_remaining, &dev_path_out[base_len]);
       rc >= 0; rc = get_next_file_path(dir, buffer_remaining,
                                        &dev_path_out[base_len])) {
    if (rc > 0) {
      fprintf(stderr, "Device path length overrun by %zd characters\n", rc);
      continue;
    }
    // open device read-only
    int fd = open_device_ro(dev_path_out);
    if (fd < 0) {
      continue;
    }

    install_dev = read_gpt(fd, &block_size);
    close(fd);

    // if we read a GPT, see if it has the entry we want
    if (install_dev != NULL && install_dev->valid) {
      *unfound_parts_out = find_install_partitions(
          install_dev, block_size, requested_parts, PATH_MAX, part_paths_out);
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
    return ZX_OK;
  } else {
    return ZX_ERR_NOT_FOUND;
  }
}

/*
 * Write out the install data from the source paths into the destination
 * paths. A partition is only written if its bit is set in both parts_requested
 * and parts_available masks. The paths_src array should be indexed based the
 * position of the bit in the masks while the paths_dest array should be
 * indexed based on how many requested partitions there are.
 */
zx_status_t write_install_data(partition_flags parts_requested,
                               partition_flags parts_available,
                               char *paths_src[], char *paths_dest[]) {
  if (unmount_all() != ZX_OK) {
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
        (CHECK_BIT(parts_requested, idx) && CHECK_BIT(parts_available, idx))) {
      continue;
    }

    // do install
    size_t bytes_written;
    int fd_dst = open(paths_dest[part_idx], O_RDWR);
    if (fd_dst == -1) {
      fprintf(stderr, "ERROR: Could not open output device for writing, %s\n",
              strerror(errno));
      return ZX_ERR_IO;
    }

    printf("writing content to '%s'\n", paths_dest[part_idx]);
    int fd_src = open(paths_src[idx], O_RDONLY);
    if (fd_src == -1) {
      fprintf(stderr, "ERROR: Could not open disk image, %s, is this"
              " the installer build?\n",
              strerror(errno));
      close(fd_dst);
      return ZX_ERR_IO;
    }

    time_t start;
    time(&start);
    zx_status_t rc = write_partition(fd_src, fd_dst, &bytes_written);
    time_t end;
    time(&end);

    printf("%.f secs taken to write %zd bytes\n", difftime(end, start),
           bytes_written);
    close(fd_dst);
    close(fd_src);

    if (rc != ZX_OK) {
      fprintf(stderr, "ERROR: Problem writing partition code: %i\n", rc);
      return rc;
    }
  }

  return ZX_OK;
}

/*
 * Given a directory, assume its contents represent block devices. Look at
 * each entry to see if it contains a GPT and if it does, see if the GPT
 * reports that space_required contiguous bytes are available. If a suitable
 * place is found device_path_out and offset_out will be set to valid values,
 * otherwise they will be left unchanged.
 */
void find_device_with_space(DIR *dir, char *dir_path, uint64_t space_required,
                            char *device_path_out, size_t *offset_out) {
  char path_buffer[PATH_MAX];
  strcpy(path_buffer, dir_path);
  size_t base_len = strlen(path_buffer);
  size_t buffer_remaining = PATH_MAX - base_len - 1;
  uint64_t block_size;

  // no device looks configured the way we want for install, see if we can
  // partition a device and make it suitable
  for (ssize_t rc =
           get_next_file_path(dir, buffer_remaining, &path_buffer[base_len]);
       rc >= 0;
       rc = get_next_file_path(dir, buffer_remaining, &path_buffer[base_len])) {
    if (rc > 0) {
      fprintf(stderr, "Device path length overrun by %zd characters\n", rc);
      continue;
    }

    // open device read-only
    int device_fd = open_device_ro(&path_buffer[0]);
    if (device_fd < 0) {
      fprintf(stderr, "Error reading directory");
      continue;
    }

    block_info_t info;
    rc = ioctl_block_get_info(device_fd, &info);
    if (rc < 0) {
      fprintf(stderr, "Unable to get block info for '%s'\n", path_buffer);
      close(device_fd);
      continue;
    }
    block_size = info.block_size;

    gpt_device_t *install_dev = read_gpt(device_fd, &block_size);

    if (install_dev == NULL) {
      close(device_fd);
      continue;
    } else if (!install_dev->valid) {
      fprintf(stderr, "Read GPT for %s, but it is invalid\n", path_buffer);
      gpt_device_release(install_dev);
      close(device_fd);
      continue;
    }

    part_location_t space_offset;
    find_available_space(install_dev, space_required / block_size,
                         info.block_count, block_size, &space_offset);
    gpt_device_release(install_dev);
    close(device_fd);
    if (space_offset.blk_len * block_size >= space_required) {
      strcpy(device_path_out, path_buffer);
      *offset_out = space_offset.blk_offset;
      return;
    }
  }
}

/*
 * Create the system partition and ESP on the specified device, starting at the
 * specified block offset.
 */
zx_status_t create_partitions(char *dev_path, uint64_t block_offset) {
  printf("Adding partitions...\n");
  // open a read/write fd for the block device
  int rw_dev = open(dev_path, O_RDWR);
  if (rw_dev < 0) {
    fprintf(stderr, "couldn't open device read/write\n");
    return ZX_ERR_IO;
  }
  uint64_t block_size;
  gpt_device_t *gpt_edit = read_gpt(rw_dev, &block_size);

  // TODO(jmatt): consider asking the user what device to partition
  // install_dev should point to the device we want to modify
  uint64_t size_blocks = MIN_SIZE_SYSTEM_PART / block_size;
  uint8_t type_system[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
  zx_status_t rc =
      add_partition(gpt_edit, block_offset, size_blocks, type_system, "system");
  if (rc != ZX_OK) {
    gpt_device_release(gpt_edit);
    close(rw_dev);
    return rc;
  }

  uint64_t size_blocks_efi = MIN_SIZE_EFI_PART / block_size;
  uint8_t type_efi[GPT_GUID_LEN] = GUID_EFI_VALUE;
  rc = add_partition(gpt_edit, block_offset + size_blocks, size_blocks_efi,
                     type_efi, "EFI");
  if (rc != ZX_OK) {
    gpt_device_release(gpt_edit);
    close(rw_dev);
    return rc;
  }

  gpt_device_release(gpt_edit);

  // force a re-read of the block device so the new partitions are
  // properly picked up
  ioctl_block_rr_part(rw_dev);
  close(rw_dev);
  return ZX_OK;
}

/*
 * Given a file descriptor open on a GPT device, checks if that GPT has an
 * entry whose type GUID matches the provided GUID.
 * Returns:
 *  ZX_OK if the data partition is found
 *  ZX_ERR_NOT_FOUND if we were able to look for the partition, but couldn't find
 *    it.
 *  ZX_ERR_IO if we were unable to read the partition table from device_fd
 */
static zx_status_t check_for_partition(int device_fd,
                                       uint8_t (*guid)[GPT_GUID_LEN]) {
  gpt_device_t *gpt_edit;
  uint64_t block_size;
  gpt_edit = read_gpt(device_fd, &block_size);
  if (gpt_edit == NULL) {
    fprintf(stderr, "Unable to read GPT from device.\n");
    return ZX_ERR_IO;
  }

  uint16_t part_count = count_partitions(gpt_edit);
  uint16_t part_idx = 0;
  zx_status_t rc = find_partition_entries(
      (gpt_partition_t **)&gpt_edit->partitions, guid, part_count, &part_idx);
  return rc;
}

/*
 * Give a partition table struct and a file descriptor pointing to a disk,
 * find the block offset and appropriate number of blocks for the partition.
 * The size returned in len_out will be at least size_min bytes and at most size
 * size_pref bytes.
 *
 * If metadata can not be read from the disk or the disk contains less than
 * size_min free space, ZX_ERR_NOT_FOUND is returned.
 */
static zx_status_t get_part_size(gpt_device_t *dev, int device_fd,
                                 uint64_t size_pref, uint64_t size_min,
                                 size_t *offset_out, size_t *len_out) {
  part_location_t part_data;
  part_data.blk_offset = 0;
  part_data.blk_len = 0;

  block_info_t info;
  ssize_t rc = ioctl_block_get_info(device_fd, &info);
  if (rc < 0) {
    return ZX_ERR_NOT_FOUND;
  }

  uint64_t num_blocks_pref = size_pref / info.block_size;
  uint64_t num_blocks_min = size_min / info.block_size;
  find_available_space(dev, num_blocks_pref, info.block_count, info.block_size,
                       &part_data);

  if (part_data.blk_len < num_blocks_min) {
    return ZX_ERR_NOT_FOUND;
  }

  *len_out = part_data.blk_len >= num_blocks_pref ? num_blocks_pref
                                                  : part_data.blk_len;
  *offset_out = part_data.blk_offset;
  return ZX_OK;
}

/*
 * Given a device struct, a file descriptor that is open on it's block device,
 * a block location, and number of blocks, create a partition entry in the GPT
 * for the partition and format that partition as the requested format with the
 * supplied label.
 */
static zx_status_t make_part(int device_fd, const char *dev_dir_path,
                             size_t offset, size_t length,
                             uint8_t (*guid)[GPT_GUID_LEN], disk_format_t format,
                             const char *label) {
  uint64_t block_size;
  uint8_t disk_guid[GPT_GUID_LEN];

  // ADD the data partition of the requested size that the requested location
  gpt_device_t *gpt_edit = read_gpt(device_fd, &block_size);
  if (gpt_edit == NULL) {
    fprintf(stderr, "Couldn't read GPT from device.\n");
    return ZX_ERR_IO;
  }

  gpt_device_get_header_guid(gpt_edit, &disk_guid);
  zx_status_t rc = add_partition(gpt_edit, offset, length, *guid, label);
  gpt_device_release(gpt_edit);
  if (rc != ZX_OK) {
    fprintf(stderr, "Partition entry could not be added to GPT.\n");
    return ZX_ERR_IO;
  }

  ssize_t ioctl_rc = ioctl_block_rr_part(device_fd);
  if (ioctl_rc < 0) {
    fprintf(stderr, "Unknown error re-reading GPT.\n");
    return ZX_ERR_IO;
  }
  // a brief pause is required while the system absorbs the GPT change
  sleep(1);
  unmount_all();

  // find the new path of the device
  DIR* dirfp = opendir(dev_dir_path);
  if (dirfp == NULL) {
    fprintf(stderr, "Couldn't open devices directory to read\n");
    return ZX_ERR_IO;
  }

  char disk_path[PATH_MAX];
  rc = find_disk_by_guid(dirfp, dev_dir_path, &disk_guid, &gpt_edit, disk_path,
                         PATH_MAX);

  closedir(dirfp);
  if (rc != ZX_OK) {
    fprintf(stderr, "Couldn't locate disk after adding partition.\n");
    return rc;
  }
  device_fd = open(disk_path, O_RDWR);

  if (device_fd < 0) {
    fprintf(stderr, "Couldn't open rebound device.\n");
    return ZX_ERR_IO;
  }

  gpt_edit = read_gpt(device_fd, &block_size);
  close(device_fd);
  if (gpt_edit == NULL) {
    fprintf(stderr, "Couldn't read GPT after partition addition.\n");
    return ZX_ERR_IO;
  }

  // count the number of partitions we have
  uint16_t part_count = count_partitions(gpt_edit);

  // locate the metadata for the partition just created
  uint16_t part_idx = 0;
  rc = find_partition_entries((gpt_partition_t **)&gpt_edit->partitions, guid,
                              part_count, &part_idx);
  if (rc != ZX_OK) {
    fprintf(stderr, "Partition that was just created is not found.\n");
    gpt_device_release(gpt_edit);
    return ZX_ERR_NOT_FOUND;
  }

  // find the partition in the block device directory
  char part_path[PATH_MAX];
  char *path_holder[1] = {part_path};
  DIR *dir = opendir(PATH_BLOCKDEVS);
  gpt_partition_t *const *ptr_cpy = &gpt_edit->partitions[part_idx];
  rc = find_partition_path(ptr_cpy, path_holder, dir, 1);
  gpt_device_release(gpt_edit);

  if (rc != ZX_OK) {
    fprintf(stderr, "Problem finding partition path.\n");
    return ZX_ERR_INTERNAL;
  }

  // construct the full path in-place now that we know which device it is
  size_t len_temp = strlen(part_path) + 1;
  size_t total_len = strlen(part_path) + strlen(dev_dir_path) + 1;

  // check that the total length required does not exceed available space AND
  // that accounting for the length of dev_dir_path we can copy part_path
  // around without source and destination regions not overlapping for memcpy
  if (total_len > PATH_MAX) {
    fprintf(stderr, "Device path is too long!\n");
    return ZX_ERR_INTERNAL;
  }

  // move the device-specific part to make space for the prefix
  memmove(&part_path[strlen(dev_dir_path)], part_path, len_temp);
  memcpy(part_path, dev_dir_path, strlen(dev_dir_path));

  // kick off formatting of the device
  rc = mkfs(part_path, format, launch_stdio_sync, &default_mkfs_options);
  if (rc != ZX_OK) {
    fprintf(stderr, "ERROR: Partition formatting failed.\n");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

static zx_status_t format_existing(int device_fd, char *dev_dir_path,
                                   uint8_t (*guid)[GPT_GUID_LEN],
                                   disk_format_t disk_format) {
    lseek(device_fd, 0, SEEK_SET);
    uint64_t block_size;

    gpt_device_t *gpt_device = read_gpt(device_fd, &block_size);
    if (gpt_device == NULL) {
      fprintf(stderr, "WARNING: Couldn't read GPT to format partition.\n");
      return ZX_ERR_INTERNAL;
    }
    uint16_t part_id;
    uint16_t part_count = count_partitions(gpt_device);
    int rc = find_partition_entries((gpt_partition_t**)&gpt_device->partitions,
                                    guid, part_count, &part_id);
    if (rc != ZX_OK) {
      gpt_device_release(gpt_device);
      fprintf(stderr, "WARNING: Couldn't find partition to format.\n");
      return ZX_ERR_INTERNAL;
    }

    char part_path[PATH_MAX];
    // find_partition_path wants an array of pointers, so we pass a pointer
    // to the address of the start of the array
    char *indir = &part_path[0];
    DIR *dev_dir;
    dev_dir = opendir(dev_dir_path);
    if (dev_dir == NULL) {
      fprintf(stderr, "WARNING: Couldn't open device directory.\n");
      gpt_device_release(gpt_device);
      return ZX_ERR_INTERNAL;
    }
    rc = find_partition_path((gpt_partition_t**)&gpt_device->partitions[part_id],
                             &indir, dev_dir, 1);
    if (rc != ZX_OK) {
      gpt_device_release(gpt_device);
      fprintf(stderr, "WARNING: Couldn't locate partition path.\n");
      return ZX_ERR_INTERNAL;
    }

    // construct the full path in-place now that we know which device it is
    size_t len_temp = strlen(part_path) + 1;
    size_t total_len = strlen(part_path) + strlen(dev_dir_path) + 1;

    // check that the total length required does not exceed available space AND
    // that accounting for the length of dev_dir_path we can copy part_path
    // around without source and destination regions not overlapping for memcpy
    if (total_len > PATH_MAX) {
      gpt_device_release(gpt_device);
      fprintf(stderr, "WARNING: Device path is too long!\n");
      return ZX_ERR_INTERNAL;
    }

    // move the device-specific part to make space for the prefix
    memmove(&part_path[strlen(dev_dir_path)], part_path, len_temp);
    memcpy(part_path, dev_dir_path, strlen(dev_dir_path));

    gpt_device_release(gpt_device);
    return mkfs(part_path, disk_format, launch_stdio_sync, &default_mkfs_options);
}

/*
 * Given a GPT device struct and a path to the disk device, check to see if
 * there is already a partition with the supplied GUID. If not, try to create
 * that partition with the given size and format.
 *
 * This returns ZX_OK if there already is a partition or if there is enough
 * space to create one and it is successfully created and formatted, otherwise
 * returns an error.
 */
static zx_status_t make_empty_partition(gpt_device_t *install_dev,
                                        char *device_path, char *dev_dir_path,
                                        uint8_t (*guid)[GPT_GUID_LEN],
                                        uint64_t size_pref, uint64_t size_min,
                                        disk_format_t disk_format, const char *name,
                                        bool reformat) {
  int device_fd = open(device_path, O_RDWR);
  if (device_fd < 0) {
    printf("WARNING: Problem opening device, '%s' partition not created.\n",
           name);
    return ZX_ERR_IO;
  }

  zx_status_t rc;
  if ((rc = check_for_partition(device_fd, guid)) == ZX_ERR_NOT_FOUND) {
    size_t blk_off;
    size_t blk_len;
    if ((get_part_size(install_dev, device_fd, size_pref, size_min, &blk_off,
                       &blk_len) != ZX_OK) ||
        (make_part(device_fd, dev_dir_path, blk_off, blk_len, guid, disk_format, name) !=
         ZX_OK)) {
      close(device_fd);
      return ZX_ERR_INTERNAL;
    }
  } else if (rc != ZX_OK) {
    fprintf(stderr, "Unexpected error '%i' looking for '%s' partition\n", rc,
            name);
    close(device_fd);
    return rc;
  } else if (rc == ZX_OK && reformat) {
    lseek(device_fd, 0, SEEK_SET);
    rc = format_existing(device_fd, dev_dir_path, guid, disk_format);
    if (rc != ZX_OK) {
      printf("WARNING: couldn't format existing partition\n");
      close(device_fd);
      return rc;
    }
  }

  close(device_fd);
  return ZX_OK;
}

static char *utf16_to_cstring(char *dst, const uint16_t *src, size_t len) {
  size_t i = 0;
  char *ptr = dst;
  while (i < len) {
    char c = src[i++] & 0x7f;
    *ptr++ = c;
    if (!c)
      break;
  }
  return dst;
}

static void get_input(char *buf, size_t max_input) {
  // converted to signed since idx could possible be negative
  ssize_t max_input_s = max_input;
  for (ssize_t idx = 0; idx < max_input_s; idx++) {
    int c = getc(stdin);

    // if the user hit backspace, erase the previous char and position the
    // index before the erased char so the next loop will fill in that space
    if (c == 8) {
      if (idx > 0) {
        printf("%c", c);
        fflush(stdout);
        idx--;
        buf[idx] = ' ';
      }
      idx--;
      continue;
    }

    sprintf(buf + idx, "%c", c);
    printf("%c", c);
    fflush(stdout);
    if (buf[idx] == '\n') {
      buf[idx] = '\0';
      return;
    }
  }

  buf[max_input - 1] = '\0';
}

/*
 * Checks that the given string converts to a number and that the number
 * converted back to a string matches the original input string.
 */
static bool check_input(char *input, int *out_value) {
  char *parsed = NULL;
  // according to strtol() docs, set errno before calling
  errno = 0;
  long int tmp = strtol(input, &parsed, 10);

  // check that there was no parse error, that the value is within the integer
  // range, and that all of the input was consumed in parsing
  if (!errno && tmp <= INT_MAX && parsed == input + strlen(input)) {
    *out_value = (int)tmp;
    return true;
  } else {
    return false;
  }
}

static void release_disk_list(list_node_t *list) {
  disk_rec_t *rec;
  while ((rec = list_remove_head_type(list, disk_rec_t, disk_node)) != NULL) {
    if (rec->path != NULL) {
      free(rec->path);
    }

    if (rec->device != NULL) {
      gpt_device_release(rec->device);
    }

    free(rec);
  }
}

/*
 * Given a size in bytes, compute how many gibibytes (2^30) and tenths of a
 * gibibyte this represents. Note that the tenths are computed by TRUNCATING
 * rather than rounding, so what would accurately be 2.46GiB will be returned
 * as 2.4GiB.
 */
static void get_gib_and_tenths(uint64_t size, uint64_t *gb_out,
                               uint64_t *tenths_out) {
  *gb_out = size >> 30;
  *tenths_out = ((size - (*gb_out << 30)) * 10) >> 30;
}

static void print_gpt(gpt_device_t *device, uint64_t block_size,
                      uint16_t part_count) {
  for (int part_idx = 0; part_idx < part_count; part_idx++) {
    gpt_partition_t *part_targ = device->partitions[part_idx];
    uint64_t size_bytes = block_size * (part_targ->last - part_targ->first + 1);
    uint64_t size_gib;
    uint64_t remainder;
    get_gib_and_tenths(size_bytes, &size_gib, &remainder);

    char name[GPT_GUID_STRLEN];
    memset(name, 0, GPT_GUID_STRLEN);

    utf16_to_cstring(name, (const uint16_t *)part_targ->name,
                     GPT_GUID_STRLEN - 1);
    name[GPT_GUID_STRLEN - 1] = '\0';

    printf("       Partition %d %16s %" PRIu64 ".%" PRIu64
           "GB at block %" PRIu64 "\n",
           part_idx, name, size_gib, remainder, part_targ->first);
  }
}

/*
 * Takes a file descriptor pointing to a disk, attempting to read a GPT from the
 * disk and construct a disk_rec_t, a pointer to which will be returned through
 * rec_out.
 * Returns
 *  ZX_ERR_NO_MEMORY if memory can't be allocated for the record data
 *  ZX_ERR_IO if the device does not contain a GPT or otherwise can not be read
 *  ZX_OK if all goes well
 */
static zx_status_t build_disk_record(int device_fd, char *path,
                                     uint64_t *block_size_out,
                                     disk_rec_t **rec_out) {
  disk_rec_t *disk_rec = malloc(sizeof(disk_rec_t));
  if (disk_rec == NULL) {
    fprintf(stderr, "No memory available to add disk entry.\n");
    return ZX_ERR_NO_MEMORY;
  }

  // see if this block device has a GPT we can read and get disk
  // size
  gpt_device_t *target_dev = read_gpt(device_fd, block_size_out);
  if (target_dev == NULL || !target_dev->valid) {
    if (target_dev != NULL) {
      gpt_device_release(target_dev);
    }
    free(disk_rec);
    return ZX_ERR_IO;
  }
  disk_rec->device = target_dev;

  // record path to the device
  disk_rec->path = malloc((strlen(path) + 1) * sizeof(char));
  if (disk_rec->path == NULL) {
    fprintf(stderr, "Out of memory when writing disk path.\n");
    free(disk_rec);
    gpt_device_release(target_dev);
    return ZX_ERR_NO_MEMORY;
  } else {
    strcpy(disk_rec->path, path);
  }

  disk_rec->part_count = count_partitions(target_dev);

  *rec_out = disk_rec;
  return ZX_OK;
}

void print_disk_info(int disk_fd, uint16_t disk_num, char *dev_path) {
  block_info_t info;
  uint64_t disk_size;
  ssize_t rc = ioctl_block_get_info(disk_fd, &info);
  if (rc < 0) {
    printf("WARNING: Unable to read disk size, reporting zero.\n");
    disk_size = 0;
  } else {
    disk_size = info.block_size * info.block_count;
  }

  uint64_t disk_size_gib;
  uint64_t tenths_of_gib;
  get_gib_and_tenths(disk_size, &disk_size_gib, &tenths_of_gib);
  printf("Disk %d (%s) %" PRIu64 ".%" PRIu64 "GB\n", disk_num, dev_path,
         disk_size_gib, tenths_of_gib);
}

static bool build_disk_list(DIR *dev_dir, char *dev_path, size_t path_buf_sz,
                            list_node_t *disk_list, uint16_t *disk_num,
                            bool print) {
  size_t base_len = strlen(dev_path);
  size_t buffer_remaining = path_buf_sz - base_len - 1;
  uint64_t block_size;
  for (ssize_t rc =
           get_next_file_path(dev_dir, buffer_remaining, &dev_path[base_len]);
       rc >= 0; rc = get_next_file_path(dev_dir, buffer_remaining,
                                        &dev_path[base_len])) {
    // confirm path to device is not too long
    if (rc > 0) {
      fprintf(stderr, "Device path length overrun by %zd characters\n", rc);
      continue;
    }

    int device_fd = open_device_ro(dev_path);
    if (device_fd < 0) {
      fprintf(stderr, "Could not read device entry.\n");
      continue;
    }

    disk_rec_t *disk_rec;
    rc = build_disk_record(device_fd, dev_path, &block_size, &disk_rec);

    if (rc == ZX_OK) {
      list_add_tail(disk_list, &disk_rec->disk_node);
    } else if (rc == ZX_ERR_IO) {
      // this just wasn't a GPT device or device couldn't be read,
      // continue on to other possible devices
      close(device_fd);
      continue;
    } else {
      close(device_fd);
      fprintf(stderr, "Out of memory or other unexpected error, aborting.\n");
      return false;
    }

    // TODO (jmatt) make print a callback
    if (print) {
      print_disk_info(device_fd, *disk_num, dev_path);
      print_gpt(disk_rec->device, block_size, disk_rec->part_count);
    }

    close(device_fd);
    (*disk_num)++;
  }

  return true;
}

static bool ask_for_disk_part(list_node_t *list, int num_disks,
                              disk_rec_t **disk_out, int *part_idx_out) {
  printf("Delete a partition on which disk (0-%i blank to cancel)?\n",
         (num_disks - 1));
  char buffer[512];
  get_input(buffer, sizeof(buffer));
  int req_disk;

  if (!check_input(buffer, &req_disk)) {
    printf("Disk selection is not understood.\n");
    return false;
  }

  // check that specified disk number is in range
  if (req_disk < 0 || req_disk >= num_disks) {
    printf("Specified disk is invalid, please choose 0-%i\n", num_disks - 1);
    return false;
  }

  disk_rec_t *selected_disk = list_peek_head_type(list, disk_rec_t, disk_node);
  for (int idx = 0; idx < req_disk; idx++) {
    selected_disk =
        list_next_type(list, &selected_disk->disk_node, disk_rec_t, disk_node);
  }

  printf("Which partition would you like to remove? (0-%i)\n",
         selected_disk->part_count - 1);

  get_input(buffer, sizeof(buffer));
  if (!check_input(buffer, &req_disk)) {
    printf("Invalid input\n");
    return false;
  }

  if (req_disk < 0 || req_disk >= selected_disk->part_count) {
    printf("Partition index is out of range, please choose 0-%i\n",
           selected_disk->part_count - 1);
    return false;
  }
  *disk_out = selected_disk;
  *part_idx_out = req_disk;
  return true;
}

static bool remove_partition(int device_fd, int part_idx) {
  uint64_t block_size;
  gpt_device_t *dev = read_gpt(device_fd, &block_size);
  if (dev == NULL) {
    printf("Unable to remove partition, couldn't read GPT.\n");
    return false;
  }

  if (!dev->valid) {
    printf("Unable to remove partition, GPT is invalid\n");
    gpt_device_release(dev);
    return false;
  }

  if (dev->partitions[part_idx] == NULL ||
      gpt_partition_remove(dev, dev->partitions[part_idx]->guid)) {
    gpt_device_release(dev);
    printf("Unable to remove partition, partition not found!\n");
    return false;
  }

  if (gpt_device_sync(dev)) {
    printf("Unable to remove partition, GPT could not be written.\n");
    gpt_device_release(dev);
    return false;
  }

  gpt_device_release(dev);
  return true;
}

static bool ask_for_space(void) {
  DIR *dev_dir;
  char dev_path[PATH_MAX] = PATH_BLOCKDEVS;
  size_t base_len = strlen(dev_path);
  assert(base_len > 0);
  dev_path[base_len] = '/';
  dev_path[++base_len] = '\0';

  dev_dir = opendir(PATH_BLOCKDEVS);
  if (dev_dir == NULL) {
    fprintf(stderr, "Could not open device directory.\n");
    return false;
  }
  list_node_t disk_list;
  list_initialize(&disk_list);
  uint16_t disk_num = 0;
  if (!build_disk_list(dev_dir, dev_path, PATH_MAX, &disk_list, &disk_num,
                       true)) {
    release_disk_list(&disk_list);
    closedir(dev_dir);
    return false;
  }
  closedir(dev_dir);

  // no disks, nothing to do
  if (disk_num < 1) {
    return false;
  }

  disk_rec_t *selected_disk;
  int req_part;
  if (!ask_for_disk_part(&disk_list, disk_num, &selected_disk, &req_part)) {
    release_disk_list(&disk_list);
    return false;
  }

  int device_fd = open(selected_disk->path, O_RDWR);
  if (device_fd < 0) {
    printf("Unable to remove partition, could not open GPT for writing.\n");
    release_disk_list(&disk_list);
    return false;
  }
  bool result = remove_partition(device_fd, req_part);

  close(device_fd);
  release_disk_list(&disk_list);
  return result;
}

static int print_summary(bool install_dev_found, bool req_data_written,
                         bool part_data_avail, bool part_blob_avail) {
  bool total_success = install_dev_found && req_data_written &&
      part_data_avail && part_blob_avail;

  printf("\n===================================\n");
  printf("INSTALL SUMMARY:      %s\n", (total_success ? "SUCCESS" : "FAILURE"));
  printf("    Drive found?      %s\n", (install_dev_found ? "YES" : "NO"));
  printf("    ESP+SYS written?  %s\n", (req_data_written ? "YES" : "NO"));
  printf("    /data ready?      %s\n", (part_data_avail ? "YES" : "NO"));
  printf("    /blobstore ready? %s\n", (part_blob_avail ? "YES" : "NO"));

  return total_success ? 0 : -1;
}

int main(int argc, char **argv) {
  static_assert(NUM_INSTALL_PARTS == 2,
                "Install partition count is unexpected, expected 2.");
  static_assert(PATH_MAX >= sizeof(PATH_BLOCKDEVS),
                "File path max length is too short for path to block devices.");

  bool wipe = false;
  if (argc == 2 && strcmp("-w", argv[1]) == 0) {
    printf("running with wipe");
    wipe = true;
  }

  // setup the base path in the path buffer
  static char path_buffer[sizeof(PATH_BLOCKDEVS) + 2];
  strcpy(path_buffer, PATH_BLOCKDEVS);
  strcat(path_buffer, "/");

  // set up structures for hold source and destination paths for partition
  // data
  char system_path[PATH_MAX];
  char efi_path[PATH_MAX];
  char *part_paths[NUM_INSTALL_PARTS] = {efi_path, system_path};
  char system_img_path[PATH_MAX] = IMG_SYSTEM_PATH;
  char efi_img_path[PATH_MAX] = IMG_EFI_PATH;
  char *disk_img_paths[NUM_INSTALL_PARTS] = {efi_img_path, system_img_path};

  // device to install on
  gpt_device_t *install_dev = NULL;
  partition_flags ready_for_install = 0;
  partition_flags requested_parts = PART_EFI | PART_SYSTEM;
  uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
  uint8_t blobfs_guid[GPT_GUID_LEN] = GUID_BLOBFS_VALUE;
  bool install_dev_found = false;
  bool req_data_written = false;
  bool part_data_avail = false;
  bool part_blob_avail = false;

  printf("Messages tagged \"ERROR\" are fatal, others are informational.\n");

  // the dirty bit is set to true whenever the devices directory is in a
  // state where it it unknown if installation can proceed
  bool retry;
  do {
    retry = false;

    // first read the directory of block devices
    DIR *dir = opendir(PATH_BLOCKDEVS);
    if (dir == NULL) {
      fprintf(stderr, "Open failed for directory: '%s' with error %s\n",
              PATH_BLOCKDEVS, strerror(errno));
      return print_summary(install_dev_found, req_data_written, part_data_avail,
                           part_blob_avail);
    }

    char disk_path[PATH_MAX];
    zx_status_t rc = find_install_device(dir, path_buffer, requested_parts,
                                         &ready_for_install, part_paths,
                                         &install_dev, disk_path, PATH_MAX);
    closedir(dir);

    if (rc == ZX_OK && install_dev->valid) {
      install_dev_found = true;
      rc = write_install_data(requested_parts, ready_for_install,
                              disk_img_paths, part_paths);

      // Check for a data and blobfs partitions, creating if necessary.
      // Having these partitions is highly desireable, but we can live
      // without it if needed
      if (rc == ZX_OK) {
        req_data_written = true;
        // store the guid of the disk we're using
        uint8_t disk_guid[GPT_GUID_LEN];
        gpt_device_get_header_guid(install_dev, &disk_guid);

        strcpy(path_buffer, PATH_BLOCKDEVS);
        strcat(path_buffer, "/");
        if (make_empty_partition(install_dev, disk_path, path_buffer, &data_guid,
                                 PREFERRED_SIZE_DATA, MIN_SIZE_DATA,
                                 DISK_FORMAT_MINFS, "data", wipe) != ZX_OK) {
          printf("WARNING: Problem locating or creating data partition.\n");
        } else {
          part_data_avail = true;
        }

        // find the device path of the disk we're using, it will have changed
        // if we created a data partition
        strcpy(path_buffer, PATH_BLOCKDEVS);
        strcat(path_buffer, "/");
        dir = opendir(path_buffer);
        if (dir == NULL) {
          printf("Unable to re-open block device directory, can not make\
                 blobfs partition");
          return print_summary(install_dev_found, req_data_written,
                               part_data_avail, part_blob_avail);
        }
        gpt_device_release(install_dev);
        find_disk_by_guid(dir, path_buffer, &disk_guid, &install_dev, disk_path,
                          PATH_MAX);
        closedir(dir);

        // add a blobfs partition
        if (make_empty_partition(install_dev, disk_path, path_buffer,
                                 &blobfs_guid, PREFERRED_SIZE_DATA,
                                 MIN_SIZE_DATA, DISK_FORMAT_BLOBFS, "blobfs", wipe)
            != ZX_OK) {
          printf("WARNING: Problem locating or creating blobfs partition.\n");
        } else {
          part_blob_avail = true;
        }
      } else {
        gpt_device_release(install_dev);
        fprintf(stderr, "Failure writing install data, aborting.\n");
        return print_summary(install_dev_found, req_data_written,
                             part_data_avail, part_blob_avail);
      }

      gpt_device_release(install_dev);
      // we ignore whether or not we could make the data partition since
      // it is desired, but not required
      return print_summary(install_dev_found, req_data_written, part_data_avail,
                           part_blob_avail);
    } else {
      dir = opendir(PATH_BLOCKDEVS);
      if (dir == NULL) {
        fprintf(stderr, "Open failed for directory: '%s' with error %s\n",
                PATH_BLOCKDEVS, strerror(errno));
        return print_summary(install_dev_found, req_data_written,
                             part_data_avail, part_blob_avail);
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
        retry = ask_for_space();
        continue;
      }

      // if partition creation succeeds, set the dirty bit
      retry = create_partitions(device_path, space_offset) == ZX_OK;

      // if we're going to try again, give the system a moment to absorb
      // newly created partitions
      if (retry) {
        sleep(1);
      }
    }
  } while (retry);

  return print_summary(install_dev_found, req_data_written, part_data_avail,
                       part_blob_avail);
}
