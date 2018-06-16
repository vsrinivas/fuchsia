// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#include <zircon/device/block.h>
#include <zircon/syscalls.h>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>

#include <unittest/unittest.h>

#define TOTAL_BLOCKS 244277248 // roughly 116GB
#define BLOCK_SIZE 512
#define SZ_FW_PART (8 * ((uint64_t)1) << 20)
#define SZ_EFI_PART (32 * ((uint64_t)1) << 20)
#define SZ_KERN_PART (16 * ((uint64_t)1) << 20)
#define SZ_FVM_PART (8 * ((uint64_t)1) << 30)

uint8_t guid_state[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;
uint8_t cros_kern[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
uint8_t cros_root[GPT_GUID_LEN] = GUID_CROS_ROOT_VALUE;
uint8_t guid_gen_data[GPT_GUID_LEN] = GUID_GEN_DATA_VALUE;
uint8_t guid_fw[GPT_GUID_LEN] = GUID_CROS_FIRMWARE_VALUE;
uint8_t guid_efi[GPT_GUID_LEN] = GUID_EFI_VALUE;
uint8_t guid_fvm[GPT_GUID_LEN] = GUID_FVM_VALUE;
uint64_t c_parts_init_sz = 1;
uint64_t blk_sz_root;
uint64_t blk_sz_kern;
uint64_t blk_sz_fw;
uint64_t blk_sz_efi;
uint64_t blk_sz_fvm;
uint64_t blk_sz_kernc;
uint64_t blk_sz_rootc;

typedef struct {
    uint64_t start;
    uint64_t len;
} partition_t;

static void init_block_sizes(block_info_t* b) {
    blk_sz_root = howmany(SZ_ROOT_PART, b->block_size);
    blk_sz_kern = howmany(SZ_KERN_PART, b->block_size);
    blk_sz_fw = howmany(SZ_FW_PART, b->block_size);
    blk_sz_efi = howmany(SZ_EFI_PART, b->block_size);
    blk_sz_fvm = howmany(SZ_FVM_PART, b->block_size);
    blk_sz_kernc = howmany(SZ_ZX_PART, b->block_size);
    blk_sz_rootc = howmany(SZ_ROOT_PART, b->block_size);
}

static int make_tmp_file(char* name_buf, int sz) {
    snprintf(name_buf, sz, "/tmp/%i", rand());
    return open(name_buf, O_RDWR | O_TRUNC | O_CREAT);
}

static int prep_gpt(gpt_device_t** device_out, block_info_t* b_info) {
    char path[4096];
    memset(path, 0, 4096);

    int fd = make_tmp_file(path, 4096);
    if (fd < 0) {
        fprintf(stderr, "Failed to make temporary file\n");
        return -1;
    }
    if (ftruncate(fd, 1024 * 1024 * 65)) {
        fprintf(stderr, "File truncation failed\n");
        close(fd);
        remove(path);
        return -1;
    }
    zx_status_t rc = gpt_device_init(fd, b_info->block_size, b_info->block_count,
                                     device_out);
    if (rc < 0) {
        close(fd);
        remove(path);
        fprintf(stderr, "Init failed!!\n");
        return -1;
    }
    gpt_device_finalize(*device_out);
    close(fd);
    remove(path);
    return 0;
}

static bool create_partition(gpt_device_t* d, const char* name, uint8_t* type,
                             partition_t* p) {
    BEGIN_HELPER;
    uint8_t guid_buf[GPT_GUID_LEN];
    zx_cprng_draw(guid_buf, GPT_GUID_LEN);

    ASSERT_EQ(gpt_partition_add(d, name, type, guid_buf, p->start, p->len, 0),
              0, "Partition could not be added.");
    END_HELPER;
}

// create the KERN-A, KERN-B, ROOT-A, ROOT-B and state partitions
static bool create_kern_roots_state(gpt_device_t* device) {
    BEGIN_HELPER;
    partition_t part_defs[5];

    // this layout is patterned off observed layouts of ChromeOS devices
    // KERN-A
    part_defs[1].start = 20480;
    part_defs[1].len = blk_sz_kern;

    // ROOT-A
    part_defs[2].start = 315392;
    part_defs[2].len = blk_sz_root;

    // KERN-B
    part_defs[3].start = part_defs[1].start + part_defs[1].len;
    part_defs[3].len = blk_sz_kern;

    // ROOT-B
    part_defs[4].start = part_defs[2].start + part_defs[2].len;
    part_defs[4].len = blk_sz_root;

    part_defs[0].start = part_defs[4].start + part_defs[4].len;

    // first the rest of the disk with STATE
    uint64_t disk_start, disk_end;
    ASSERT_EQ(gpt_device_range(device, &disk_start, &disk_end), 0,
              "Retrieval of device range failed.");
    part_defs[0].len = disk_end - part_defs[0].start;


    ASSERT_TRUE(create_partition(device, "STATE", guid_state, &part_defs[0]),
                "");

    ASSERT_TRUE(create_partition(device, "KERN-A", cros_kern, &part_defs[1]),
                "");

    ASSERT_TRUE(create_partition(device, "ROOT-A", cros_root, &part_defs[2]),
                "");

    ASSERT_TRUE(create_partition(device, "KERN-B", cros_kern, &part_defs[3]),
                "");

    ASSERT_TRUE(create_partition(device, "ROOT-B", cros_root, &part_defs[4]),
                "");
    END_HELPER;
}

static bool create_default_c_parts(gpt_device_t* device) {
    BEGIN_HELPER;
    partition_t part_defs[2];
    part_defs[0].start = gpt_device_get_size_blocks(BLOCK_SIZE);
    part_defs[0].len = c_parts_init_sz;

    part_defs[1].start = part_defs[0].start + part_defs[0].len;
    part_defs[1].len = c_parts_init_sz;

    ASSERT_TRUE(create_partition(device, "KERN-C", cros_kern, &part_defs[0]),
                "");

    ASSERT_TRUE(create_partition(device, "ROOT-C", cros_root, &part_defs[1]),
                "");
    END_HELPER;
}

static bool create_misc_parts(gpt_device_t* device) {
    BEGIN_HELPER;
    partition_t part_defs[5];
    // "OEM"
    part_defs[0].start = 86016;
    part_defs[0].len = blk_sz_kern;

    // "reserved"
    part_defs[1].start = 16450;
    part_defs[1].len = 1;

    // "reserved"
    part_defs[2].start = part_defs[0].start + part_defs[0].len;
    part_defs[2].len = 1;

    // "RWFW"
    part_defs[3].start = 64;
    part_defs[3].len = blk_sz_fw;

    // "EFI-SYSTEM"
    part_defs[4].start = 249856;
    part_defs[4].len = blk_sz_efi;

    ASSERT_TRUE(create_partition(device, "OEM", guid_gen_data, &part_defs[0]),
                "");

    ASSERT_TRUE(create_partition(device, "reserved", guid_gen_data, &part_defs[1]),
                "");

    ASSERT_TRUE(create_partition(device, "reserved", guid_gen_data, &part_defs[2]),
                "");

    ASSERT_TRUE(create_partition(device, "RWFW", guid_fw, &part_defs[3]), "");

    ASSERT_TRUE(create_partition(device, "EFI-SYSTEM", guid_efi, &part_defs[4]),
                "");
    END_HELPER;
}

static bool create_test_layout(gpt_device_t* device) {
    BEGIN_HELPER;
    ASSERT_TRUE(create_kern_roots_state(device), "");

    ASSERT_TRUE(create_default_c_parts(device), "");

    ASSERT_TRUE(create_misc_parts(device), "");
    END_HELPER;
}

static bool add_fvm_part(gpt_device_t* device, gpt_partition_t* state) {
    BEGIN_HELPER;
    partition_t fvm_part;
    fvm_part.start = state->first;
    fvm_part.len = blk_sz_fvm;

    state->first += blk_sz_fvm;

    ASSERT_TRUE(create_partition(device, "FVM", guid_fvm, &fvm_part), "");

    END_HELPER;
}

static void resize_kernc_from_state(gpt_partition_t* kernc,
                                    gpt_partition_t* state) {
    kernc->first = state->first;
    kernc->last = kernc->first + blk_sz_kernc - 1;
    state->first = kernc->last + 1;
}

static void resize_rootc_from_state(gpt_partition_t* rootc,
                                    gpt_partition_t* state) {
    rootc->first = state->first;
    rootc->last = rootc->first + blk_sz_rootc - 1;
    state->first = rootc->last + 1;
}

// assumes that the base layout contains 12 partitions and that
// partition 0 is the resizable state parition
// the fvm partition will be created as the 13th partition
static bool create_test_layout_with_fvm(gpt_device_t* device) {
    BEGIN_HELPER;
    ASSERT_TRUE(create_test_layout(device), "");

    ASSERT_TRUE(add_fvm_part(device, device->partitions[0]), "");
    END_HELPER;
}

static bool init_test_env(gpt_device_t** d_out, block_info_t* b_out) {
    BEGIN_HELPER;
    b_out->block_count = TOTAL_BLOCKS;
    b_out->block_size = BLOCK_SIZE;
    init_block_sizes(b_out);

    ASSERT_EQ(prep_gpt(d_out, b_out), 0, "Basic test setup failed.");
    END_HELPER;
}

bool test_default_config(void) {
    BEGIN_TEST;
    block_info_t b_info;
    gpt_device_t* dev;
    ASSERT_TRUE(init_test_env(&dev, &b_info), "");

    ASSERT_TRUE(create_test_layout(dev), "Test layout creation failed.");

    ASSERT_FALSE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                 "Device SHOULD NOT be ready to pave.");
    ASSERT_EQ(config_cros_for_fuchsia(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART,
                                      true),
              ZX_OK, "Configuration failed.");
    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Device SHOULD be ready to pave.");

    gpt_device_release(dev);
    END_TEST;
}

bool test_already_configured(void) {
    BEGIN_TEST;
    block_info_t b_info;
    gpt_device_t* dev;
    ASSERT_TRUE(init_test_env(&dev, &b_info), "");

    ASSERT_TRUE(create_test_layout(dev), "Test layout creation failed.");
    ASSERT_TRUE(add_fvm_part(dev, dev->partitions[0]),
                "Could not add FVM partition record");
    resize_kernc_from_state(dev->partitions[5], dev->partitions[0]);
    resize_rootc_from_state(dev->partitions[6], dev->partitions[0]);

    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Device SHOULD NOT be ready to pave.");

    // TODO verify that nothing changed
    ASSERT_EQ(config_cros_for_fuchsia(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART,
                                      true),
              ZX_OK, "Config failed.");

    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Device SHOULD be ready to pave.");
    gpt_device_release(dev);
    END_TEST;
}

bool test_no_c_parts(void) {
    BEGIN_TEST;
    block_info_t b_info;
    gpt_device_t* dev;
    ASSERT_TRUE(init_test_env(&dev, &b_info), "");

    ASSERT_TRUE(create_kern_roots_state(dev),
                "Couldn't create A/B kern and root parts");

    ASSERT_TRUE(create_misc_parts(dev), "Couldn't create misc parts");

    ASSERT_FALSE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                 "Should not initially be ready to pave");

    ASSERT_EQ(config_cros_for_fuchsia(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART,
                                      SZ_FVM_PART),
              ZX_OK, "Configure failed");

    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Device should now be ready to pave, but isn't");
    gpt_device_release(dev);
    END_TEST;
}

bool test_no_rootc(void) {
    BEGIN_TEST;
    block_info_t b_info;
    gpt_device_t* dev;
    ASSERT_TRUE(init_test_env(&dev, &b_info), "");

    ASSERT_TRUE(create_kern_roots_state(dev),
                "Couldn't make A&B kern/root parts");

    ASSERT_TRUE(create_misc_parts(dev), "Couldn't create misc parts");

    ASSERT_TRUE(create_default_c_parts(dev), "Couldn't create c parts\n");

    ASSERT_EQ(gpt_partition_remove(dev, dev->partitions[11]->guid), 0,
              "Failed to remove ROOT-C partition");

    ASSERT_FALSE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                 "Should not initially be ready to pave");

    ASSERT_EQ(config_cros_for_fuchsia(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART,
                                      true),
              ZX_OK, "Configure failed");

    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Device should now be ready to pave, but isn't");
    gpt_device_release(dev);
    END_TEST;
}

bool test_no_kernc(void) {
    BEGIN_TEST;
    block_info_t b_info;
    gpt_device_t* dev;
    ASSERT_TRUE(init_test_env(&dev, &b_info), "");

    ASSERT_TRUE(create_kern_roots_state(dev),
                "Couldn't make A&B kern/root parts");

    ASSERT_TRUE(create_misc_parts(dev), "Couldn't create misc parts");

    ASSERT_TRUE(create_default_c_parts(dev), "Couldn't create c parts\n");

    ASSERT_EQ(gpt_partition_remove(dev, dev->partitions[10]->guid), 0,
              "Failed to remove ROOT-C partition");

    ASSERT_FALSE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                 "Should not initially be ready to pave");

    ASSERT_EQ(config_cros_for_fuchsia(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART,
                                      true),
              ZX_OK, "Configure failed");

    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Device should now be ready to pave, but isn't");
    gpt_device_release(dev);
    END_TEST;
}

bool test_ready_with_extra_space(void) {
    BEGIN_TEST;
    block_info_t b_info;
    gpt_device_t* dev;
    ASSERT_TRUE(init_test_env(&dev, &b_info), "");

    ASSERT_TRUE(create_test_layout(dev), "Couldn't make test layout.");
    resize_kernc_from_state(dev->partitions[5], dev->partitions[0]);
    resize_rootc_from_state(dev->partitions[6], dev->partitions[0]);
    ASSERT_TRUE(add_fvm_part(dev, dev->partitions[0]),
                "Couldn't add FVM partition.");

    const uint64_t block_gap = 1000000;
    // make some free space
    dev->partitions[0]->first += block_gap;

    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Should initially be ready to pave");

    ASSERT_EQ(config_cros_for_fuchsia(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
              ZX_OK, "Configure failed");

    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Device should still be ready to pave, but isn't");

    char msg_buf[4096];
    uint64_t new_gap = dev->partitions[0]->first - dev->partitions[12]->last - 1;
    sprintf(msg_buf, "Gap size unexpected: %lu", new_gap);
    // verify that the gap is unchanged
    ASSERT_EQ(new_gap, block_gap, msg_buf);
    gpt_device_release(dev);
    END_TEST;
}

bool test_expand_in_place(void) {
    BEGIN_TEST;
    block_info_t b_info;
    gpt_device_t* dev;
    ASSERT_TRUE(init_test_env(&dev, &b_info), "");

    ASSERT_TRUE(create_test_layout(dev), "Couldn't make test layout.");
    ASSERT_TRUE(add_fvm_part(dev, dev->partitions[0]),
              "Couldn't add FVM partition.");

    // create free space enough free space for kern-c and root-c to go before the
    // state partition along with some additional space.
    // Then position root-c and kern-c such that they can expand in place
    const uint64_t free_blks = 1000000;
    uint64_t gap = blk_sz_rootc + blk_sz_kernc + free_blks;

    // place the kern-c partition with a big gap after it
    dev->partitions[5]->first = dev->partitions[0]->first;
    dev->partitions[5]->last = dev->partitions[5]->first;

    dev->partitions[0]->first += gap;

    // place the root-c partition at the end of the big gap
    dev->partitions[6]->first = dev->partitions[0]->first - 1;
    dev->partitions[6]->last = dev->partitions[6]->first;

    ASSERT_FALSE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                 "Should not initially be ready to pave");

    ASSERT_EQ(config_cros_for_fuchsia(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
              ZX_OK, "Configure failed");

    ASSERT_TRUE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                "Device should be ready to pave, but isn't");

    char msg_buf[4096];
    uint64_t new_gap = dev->partitions[6]->first - dev->partitions[5]->last - 1;
    sprintf(msg_buf, "Gap size unexpected: %lu", new_gap);
    // verify that the gap is unchanged
    ASSERT_EQ(new_gap, free_blks, msg_buf);
    gpt_device_release(dev);
    END_TEST;
}

bool test_disk_too_small(void) {
    BEGIN_TEST;

    // first setup the device as though it is a normal test so we can compute
    // the blocks required
    block_info_t b_info;
    b_info.block_count = TOTAL_BLOCKS;
    b_info.block_size = BLOCK_SIZE;
    init_block_sizes(&b_info);

    gpt_device_t* dev;
    ASSERT_EQ(prep_gpt(&dev, &b_info), 0, "Failed doing first GPT prep");

    ASSERT_TRUE(create_test_layout(dev), "Failed creating initial test layout");

    uint32_t reserved = gpt_device_get_size_blocks(b_info.block_size);
    // this is the size we need the STATE parition to be if we are to resize
    // it to make room for the partitions we want to add and expand
    uint64_t needed_blks = howmany(SZ_ZX_PART + SZ_ROOT_PART + MIN_SZ_STATE,
                                   b_info.block_size) + reserved;
    // now remove a few blocks so we can't satisfy all constraints
    needed_blks--;

    b_info.block_count = dev->partitions[0]->first + needed_blks - 1;
    dev->partitions[0]->last = b_info.block_count - reserved - 1;

    // now that we've calculated the block count, create a device with that
    // smaller count

    ASSERT_EQ(prep_gpt(&dev, &b_info), 0, "Failed creating real GPT");

    ASSERT_TRUE(create_test_layout(dev), "Failed creating real test layout");

    ASSERT_FALSE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                 "Should not initially be ready to pave");

    ASSERT_NE(config_cros_for_fuchsia(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART,
                                      true),
              ZX_OK, "Configure reported success, but should have failed.");
    ASSERT_FALSE(is_ready_to_pave(dev, &b_info, SZ_ZX_PART, SZ_ROOT_PART, true),
                 "Device should still not be paveable");
    gpt_device_release(dev);
    END_TEST;
}

bool test_is_cros_device(void) {
    BEGIN_TEST;
    block_info_t b_info;
    gpt_device_t* dev;
    ASSERT_TRUE(init_test_env(&dev, &b_info), "");

    ASSERT_TRUE(create_test_layout(dev), "Failed to create test layout");

    ASSERT_TRUE(is_cros(dev), "This should be recongized as a chromeos layout");
    zx_cprng_draw(dev->partitions[1]->type, GPT_GUID_LEN);
    zx_cprng_draw(dev->partitions[4]->type, GPT_GUID_LEN);
    ASSERT_FALSE(is_cros(dev), "This should NOT be recognized as a chromos layout");
    gpt_device_release(dev);
    END_TEST;
}

BEGIN_TEST_CASE(disk_wizard_tests)
RUN_TEST(test_default_config)
RUN_TEST(test_already_configured)
RUN_TEST(test_no_c_parts)
RUN_TEST(test_no_rootc)
RUN_TEST(test_no_kernc)
RUN_TEST(test_ready_with_extra_space)
RUN_TEST(test_expand_in_place)
RUN_TEST(test_disk_too_small)
RUN_TEST(test_is_cros_device)
END_TEST_CASE(disk_wizard_tests)

int main(int argc, char** argv) {
    uint64_t seed;
    zx_cprng_draw(&seed, sizeof(seed));
    srand(seed);

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
