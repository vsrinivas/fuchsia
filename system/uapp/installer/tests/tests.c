// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.
#include <errno.h>
#include <fcntl.h>
#include <fs-management/ramdisk.h>
#include <gpt/gpt.h>
#include <inttypes.h>
#include <limits.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/syscalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <unittest/unittest.h>

#include <installer/installer.h>

#define TABLE_SIZE 6
#define BLOCK_SIZE (uint64_t)512
#define DEV_DIR_PATH (PATH_BLOCKDEVS "/")

static int create_test_ramdisk(uint64_t size, char **disk_path_out) {
  char *disk_path = malloc(sizeof(char) * PATH_MAX);
  if (disk_path == NULL) {
    fprintf(stderr, "No memory to store disk path\n");
    return -1;
  }

  if (create_ramdisk(BLOCK_SIZE, size / BLOCK_SIZE, disk_path) < 0) {
    fprintf(stderr, "RAM disk could not be created.\n");
    free(disk_path);
    return -1;
  }

  int fd = open(disk_path, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "Could not open new RAM disk\n");
    free(disk_path);
    return -1;
  }

  *disk_path_out = disk_path;
  return fd;
}

/*
 * Generate a "random" GUID. Note that you should seed srand() before
 * calling. This is also, by no means, a 'secure' random value being
 * generated.
 */
static void generate_guid(uint8_t *guid_out) {
  size_t sz;
  zx_cprng_draw(guid_out, GPT_GUID_LEN, &sz);
  assert(sz == GPT_GUID_LEN);
}

static void create_partition_table(gpt_partition_t *part_entries_out,
                                   gpt_partition_t **part_entry_table_out,
                                   size_t num_entries, uint64_t part_size,
                                   uint64_t blocks_reserved,
                                   uint64_t *total_size_out) {
  // sleep a second, just in case we're called consecutively, this will
  // give us a different random seed than any previous call
  sleep(1);
  srand(time(NULL));
  for (size_t idx = 0; idx < num_entries; idx++) {
    part_entry_table_out[idx] = &part_entries_out[idx];
    generate_guid(part_entries_out[idx].type);
    generate_guid(part_entries_out[idx].guid);
    part_entries_out[idx].first = blocks_reserved + idx * part_size;
    part_entries_out[idx].last = part_entries_out[idx].first + part_size - 1;
  }

  *total_size_out = num_entries * part_size + 2 * blocks_reserved;
}

static gpt_device_t *init_gpt(const char *dev, bool warn, int *out_fd) {
  int fd = open(dev, O_RDWR);
  if (fd < 0) {
    printf("error opening %s %i %s\n", dev, fd, strerror(errno));
    return NULL;
  }

  block_info_t info;
  ssize_t rc = ioctl_block_get_info(fd, &info);
  if (rc < 0) {
    printf("error getting block info\n");
    close(fd);
    return NULL;
  }

  gpt_device_t *gpt;
  rc = gpt_device_init(fd, info.block_size, info.block_count, &gpt);
  if (rc < 0) {
    printf("error initializing GPT\n");
    close(fd);
    return NULL;
  }

  *out_fd = fd;
  return gpt;
}

#define check_outputs(rc, dev, path, guid_targ, dir, success)                  \
  ({                                                                           \
    if (success) {                                                             \
      ASSERT_EQ(rc, ZX_OK, "Disk not found when it was expected");          \
      ASSERT_NE(dev, NULL, "Disk found, but gpt_device_t not set.");           \
      ASSERT_NE(strcmp(path, ""), 0, "Disk found, but path not set");          \
      uint8_t guid_actual[GPT_GUID_LEN];                                       \
      gpt_device_get_header_guid(dev, &guid_actual);                           \
      ASSERT_EQ(memcmp(guid_targ, guid_actual, GPT_GUID_LEN), 0,               \
                "Disk found, but GUID does not match target.");                \
      gpt_device_release(dev);                                                 \
      dev = NULL;                                                              \
    } else {                                                                   \
      ASSERT_EQ(rc, ZX_ERR_NOT_FOUND, "Disk found, but was not expected");        \
      ASSERT_EQ(dev, NULL, "Disk not found, but gpt_device_t is set.");        \
      ASSERT_EQ(strcmp(path, ""), 0, "Disk not found, but path is set");       \
    }                                                                          \
    rewinddir(dir);                                                            \
  })

/*
 * This test goes through a number of phases, each building on the last. First
 * it generates a random GUID and a RAM disk with no GPT. We then verify that
 * a search for our random GUID fails. Next we create a second disk and run
 * the search again. Then we destroy the second disk and add a GPT to the first
 * disk. Now the first disk should have a GUID in its GPT header, which should
 * not match our random GUID, we search for the random GUID again to verify it
 * isn't found. Next we read the GUID of the first disk and search for it,
 * verifying it is found. Then we create a second RAM disk, add a GPT, and
 * validate the first disk is found when searching for it. Finally we read the
 * GUID of the second disk and search for it, verifying it is found.
 *
 * In all conditions where the disk is not found, we validate that the
 * gpt_device_t out pointer is not set. In all conditions where the disk is
 * found we validate that the device pointer is set and by freeing the device
 * that it hasn't already been freed. In all conditions where the disk is found
 * we validate that the header GUID of the found device matches the requested
 * GUID.
 */
bool test_find_disk_by_guid(void) {
  BEGIN_TEST;
  DIR *dir = opendir(DEV_DIR_PATH);

  ASSERT_NE(dir, NULL, "Could not open block devices path");

  char disk_path[PATH_MAX];
  strcpy(disk_path, "");
  gpt_device_t *target;
  uint8_t guid_rand[GPT_GUID_LEN];

  // create a random GUID that we'll look for, the chances of random collision
  // are such that if it happens, we should probably look at the PRNG
  generate_guid(guid_rand);

  // presumably we have no disks attached, although even if we do, we expect
  // not to find a match
  zx_status_t rc = find_disk_by_guid(dir, DEV_DIR_PATH, &guid_rand, &target,
                                     disk_path, PATH_MAX);
  check_outputs(rc, target, disk_path, guid_rand, dir, false);

  // create a RAM disk w/o a GPT and search again, should not find
  char *disk1;
  int fd1 = create_test_ramdisk(((uint64_t)512) * 20000, &disk1);
  ASSERT_GT(fd1, -1, "");
  close(fd1);
  rc = find_disk_by_guid(dir, DEV_DIR_PATH, &guid_rand, &target, disk_path,
                         PATH_MAX);
  check_outputs(rc, target, disk_path, guid_rand, dir, false);

  // create a second RAM disk w/o GPT and search again, should not find
  char *disk2;
  int fd2 = create_test_ramdisk(((uint64_t)512) * 200000, &disk2);
  sleep(1);
  ASSERT_GT(fd2, -1, "");
  close(fd2);
  rc = find_disk_by_guid(dir, DEV_DIR_PATH, &guid_rand, &target, disk_path,
                         PATH_MAX);
  check_outputs(rc, target, disk_path, guid_rand, dir, false);

  // kill the second RAM disk to run checks when a single disk has a GPT
  destroy_ramdisk(disk2);
  free(disk2);
  sleep(1);

  // add a GPT to the single attached disk
  gpt_device_t *gpt1 = init_gpt(disk1, true, &fd1);

  ASSERT_NE(gpt1, NULL, "GPT initialization failed");
  ASSERT_GT(fd1, 0, "Invalid file descriptor returned from GPT init");
  ASSERT_EQ(gpt_device_sync(gpt1), 0, "Error writing out new GPT");
  ASSERT_EQ(ioctl_block_rr_part(fd1), 0, "Error rebinding device");
  sleep(1);

  // check that the new disk is not found when search for our random GUID
  rc = find_disk_by_guid(dir, DEV_DIR_PATH, &guid_rand, &target, disk_path,
                         PATH_MAX);
  check_outputs(rc, target, disk_path, guid_rand, dir, false);

  // read the disk's GUID and then search for it, it should be found
  uint8_t guid_known[GPT_GUID_LEN];
  gpt_device_get_header_guid(gpt1, &guid_known);
  rc = find_disk_by_guid(dir, DEV_DIR_PATH, &guid_known, &target, disk_path,
                         PATH_MAX);
  check_outputs(rc, target, disk_path, guid_known, dir, true);

  // create a second disk
  fd2 = create_test_ramdisk(((uint64_t)512) * 200000, &disk2);
  ASSERT_GT(fd2, -1, "");
  close(fd2);
  gpt_device_t *gpt2 = init_gpt(disk2, true, &fd2);
  ASSERT_NE(gpt2, NULL, "GPT initialization failed");
  ASSERT_GT(fd2, 0, "Invalid file descriptor returned from GPT init");
  ASSERT_EQ(gpt_device_sync(gpt2), 0, "Error writing out new GPT");
  ASSERT_EQ(ioctl_block_rr_part(fd2), 0, "Error rebinding device");
  sleep(1);

  // check that no disk is found when searching for the random GUID
  rc = find_disk_by_guid(dir, DEV_DIR_PATH, &guid_rand, &target, disk_path,
                         PATH_MAX);
  check_outputs(rc, target, disk_path, guid_rand, dir, false);

  // check that the first disk can be found by GUID
  rc = find_disk_by_guid(dir, DEV_DIR_PATH, &guid_known, &target, disk_path,
                         PATH_MAX);
  check_outputs(rc, target, disk_path, guid_known, dir, true);

  // read the second disk's GUID and verify it can be found
  gpt_device_get_header_guid(gpt2, &guid_known);
  rc = find_disk_by_guid(dir, DEV_DIR_PATH, &guid_known, &target, disk_path,
                         PATH_MAX);
  check_outputs(rc, target, disk_path, guid_known, dir, true);

  closedir(dir);
  free(disk1);
  free(disk2);
  END_TEST;
}

bool test_find_partition_entries(void) {
  gpt_partition_t *part_entry_ptrs[TABLE_SIZE];
  gpt_partition_t part_entries[TABLE_SIZE];
  // const uint16_t tbl_sz = 6;
  // all partitions are 4GiB worth of 512b blocks
  const uint64_t block_size = 512;
  const uint64_t part_size = (((uint64_t)1) << 32) / block_size;
  const uint64_t blocks_reserved = SIZE_RESERVED / block_size;
  uint64_t total_blocks;

  create_partition_table(part_entries, part_entry_ptrs, TABLE_SIZE, part_size,
                         blocks_reserved, &total_blocks);

  uint16_t test_indices[3] = {0, TABLE_SIZE - 1, TABLE_SIZE / 2};

  BEGIN_TEST;

  for (uint16_t idx = 0; idx < countof(test_indices); idx++) {
    uint16_t found_idx = TABLE_SIZE;
    uint16_t targ_idx = test_indices[idx];
    zx_status_t rc = find_partition_entries(
        part_entry_ptrs, &part_entries[targ_idx].type, TABLE_SIZE, &found_idx);
    ASSERT_EQ(rc, ZX_OK, "");
  }

  uint8_t random_guid[16];
  generate_guid(random_guid);
  uint16_t found_idx = TABLE_SIZE;
  zx_status_t rc = find_partition_entries(part_entry_ptrs, &random_guid,
                                          TABLE_SIZE, &found_idx);
  ASSERT_EQ(rc, ZX_ERR_NOT_FOUND, "");
  END_TEST;
}

bool test_find_partition(void) {
  gpt_partition_t *part_entry_ptrs[TABLE_SIZE];
  gpt_partition_t part_entries[TABLE_SIZE];
  const uint64_t block_size = 512;
  const uint64_t part_size = ((uint64_t)1) << 32;
  const uint64_t blocks_reserved = SIZE_RESERVED / block_size;
  uint64_t total_blocks;

  create_partition_table(part_entries, part_entry_ptrs, TABLE_SIZE,
                         part_size / block_size, blocks_reserved,
                         &total_blocks);

  uint16_t test_indices[3] = {0, TABLE_SIZE - 1, TABLE_SIZE / 2};

  BEGIN_TEST;
  for (uint16_t idx = 0; idx < countof(test_indices); idx++) {
    uint16_t targ_idx = test_indices[idx];
    uint16_t found_idx = TABLE_SIZE;
    gpt_partition_t *part_info;
    zx_status_t rc = find_partition(
        part_entry_ptrs, &part_entry_ptrs[targ_idx]->type, part_size,
        block_size, "TEST", TABLE_SIZE, &found_idx, &part_info);
    ASSERT_EQ(rc, ZX_OK, "");
    ASSERT_EQ(targ_idx, found_idx, "");
    ASSERT_BYTES_EQ((uint8_t *)part_entry_ptrs[targ_idx], (uint8_t *)part_info,
                    sizeof(gpt_partition_t), "");
  }

  // need to pass part_size in bytes, not blocks
  uint16_t found_idx = TABLE_SIZE;
  gpt_partition_t *part_info = NULL;
  zx_status_t rc =
      find_partition(part_entry_ptrs, &part_entry_ptrs[0]->type, part_size + 1,
                     block_size, "TEST", TABLE_SIZE, &found_idx, &part_info);

  ASSERT_EQ(rc, ZX_ERR_NOT_FOUND, "");
  END_TEST;
}

bool verify_sort(gpt_partition_t **partitions, int count) {
  bool ordered = true;
  for (int idx = 1; idx < count; idx++) {
    if (partitions[idx - 1]->first > partitions[idx]->first) {
      ordered = false;
      printf("Values are not ordered, index: %i--%" PRIu64 "!\n", idx,
             partitions[idx]->first);
    }
  }
  return ordered;
}

bool do_sort_test(int test_size, uint64_t val_max) {
  gpt_partition_t *values = malloc(test_size * sizeof(gpt_partition_t));
  gpt_partition_t **value_ptrs = malloc(test_size * sizeof(gpt_partition_t *));
  if (values == NULL) {
    fprintf(stderr, "Unable to allocate memory for test\n");
    return false;
  }

  for (int idx = 0; idx < test_size; idx++) {
    bool unique = false;
    while (!unique) {
      double rando = ((double)rand()) / RAND_MAX;
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

  gpt_partition_t **sorted_values = sort_partitions(value_ptrs, test_size);
  ASSERT_EQ(verify_sort(sorted_values, test_size), true, "");

  // sort again to test ordered data is handled properly
  sorted_values = sort_partitions(sorted_values, test_size);
  ASSERT_EQ(verify_sort(sorted_values, test_size), true, "");

  free(values);
  free(sorted_values);
  free(value_ptrs);

  return true;
}

bool test_sort(void) {
  BEGIN_TEST;
  // run 20 iterations with 20K elements as a stress test. We also think
  // this should hit all possible code paths
  for (int count = 0; count < 20; count++) {
    do_sort_test(256, 10000000);
    fflush(stdout);
  }
  END_TEST;
}

bool test_find_available_space(void) {
  BEGIN_TEST;
  gpt_device_t test_device;
  gpt_partition_t part_entries[TABLE_SIZE];
  memset(test_device.partitions, 0,
         sizeof(gpt_partition_t *) * PARTITIONS_COUNT);
  const uint64_t block_size = 512;
  const uint64_t blocks_reserved = SIZE_RESERVED / block_size;
  const uint64_t part_blocks = (((uint64_t)1) << 32) / block_size;
  uint64_t total_blocks;

  // create a full partition table
  create_partition_table(part_entries, test_device.partitions, TABLE_SIZE,
                         part_blocks, blocks_reserved, &total_blocks);

  part_location_t hole_location;
  find_available_space(&test_device, 1, total_blocks, block_size,
                       &hole_location);

  ASSERT_EQ(hole_location.blk_offset, (size_t)0, "");
  ASSERT_EQ(hole_location.blk_len, (size_t)0, "");

  // "expand" the disk by the required size, we should find there is space
  // at the end of the disk
  find_available_space(&test_device, part_blocks, total_blocks + part_blocks,
                       block_size, &hole_location);

  ASSERT_EQ(hole_location.blk_offset, part_entries[TABLE_SIZE - 1].last + 1,
            "");

  // "expand" the disk by not quite enough
  find_available_space(&test_device, part_blocks + 1,
                       total_blocks + part_blocks, block_size, &hole_location);

  ASSERT_EQ(hole_location.blk_len, part_blocks, "");

  // remove the first partition, but hold a reference to it
  gpt_partition_t *saved = test_device.partitions[0];
  for (int idx = 0; idx < TABLE_SIZE - 1; idx++) {
    test_device.partitions[idx] = test_device.partitions[idx + 1];
  }
  test_device.partitions[TABLE_SIZE - 1] = NULL;

  // check that space is reported at the beginning of the disk, after the
  // reserved area
  find_available_space(&test_device, part_blocks, total_blocks, block_size,
                       &hole_location);
  ASSERT_EQ(hole_location.blk_offset, blocks_reserved, "");

  // make the requested partition size just larger than available
  find_available_space(&test_device, part_blocks + 1, total_blocks, block_size,
                       &hole_location);
  ASSERT_EQ(hole_location.blk_len, part_blocks, "");

  // restore the original first partition, overwriting the original second
  // partition in the process
  test_device.partitions[0] = saved;
  find_available_space(&test_device, part_blocks, total_blocks, block_size,
                       &hole_location);
  ASSERT_EQ(hole_location.blk_offset, test_device.partitions[0]->last + 1, "");

  // again make the requested space size slightly too large
  find_available_space(&test_device, part_blocks + 1, total_blocks, block_size,
                       &hole_location);
  ASSERT_EQ(hole_location.blk_len, part_blocks, "");

  END_TEST;
}

BEGIN_TEST_CASE(installer_tests)
RUN_TEST(test_find_partition_entries)
RUN_TEST(test_find_partition)
RUN_TEST(test_sort)
RUN_TEST(test_find_available_space)
RUN_TEST(test_find_disk_by_guid)
END_TEST_CASE(installer_tests)

int main(int argc, char **argv) {
  return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
