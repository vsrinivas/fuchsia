// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that be be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/cksum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <lib/fdio/io.h>
#include <lib/zx/vmo.h>
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
#define SZ_SYSCFG_PART (1<<20)

namespace {

const uint8_t kStateGUID[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;
const uint8_t kCrosKernGUID[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
const uint8_t kCrosRootGUID[GPT_GUID_LEN] = GUID_CROS_ROOT_VALUE;
const uint8_t kGenDataGUID[GPT_GUID_LEN] = GUID_GEN_DATA_VALUE;
const uint8_t kFwGUID[GPT_GUID_LEN] = GUID_CROS_FIRMWARE_VALUE;
const uint8_t kEfiGUID[GPT_GUID_LEN] = GUID_EFI_VALUE;
const uint8_t kFvmGUID[GPT_GUID_LEN] = GUID_FVM_VALUE;
const uint64_t kCPartsInitSize = 1;

const block_info_t kDefaultBlockInfo = {
    .block_count = TOTAL_BLOCKS,
    .block_size = BLOCK_SIZE,
    .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
    .flags = 0,
    .reserved = 0,
};

class TestState {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(TestState);

    TestState(block_info_t info = kDefaultBlockInfo) : device_(nullptr) {
        Initialize(info);
    }

    void Initialize(block_info_t info) {
        ReleaseGpt();
        blk_sz_root_ = howmany(SZ_ROOT_PART, info.block_size);
        blk_sz_kern_ = howmany(SZ_KERN_PART, info.block_size);
        blk_sz_fw_ = howmany(SZ_FW_PART, info.block_size);
        blk_sz_efi_ = howmany(SZ_EFI_PART, info.block_size);
        blk_sz_fvm_ = howmany(SZ_FVM_PART, info.block_size);
        blk_sz_kernc_ = howmany(SZ_ZX_PART, info.block_size);
        blk_sz_rootc_ = howmany(SZ_ROOT_PART, info.block_size);
        block_info_ = info;
        device_ = nullptr;
    }

    uint64_t BlockCount() const {
        return block_info_.block_count;
    }

    uint64_t BlockSize() const {
        return block_info_.block_size;
    }

    bool PrepareGpt() {
        BEGIN_HELPER;
        ASSERT_EQ(device_, nullptr);
        const uint64_t sz = BlockCount() * BlockSize();
        zx::vmo vmo;
        ASSERT_EQ(zx::vmo::create(sz, 0, &vmo), ZX_OK);
        fd_.reset(fdio_vmo_fd(vmo.release(), 0, sz));
        ASSERT_TRUE(fd_);
        zx_status_t rc = gpt_device_init(fd_.get(), BlockSize(), BlockCount(),
                                         &device_);
        ASSERT_GE(rc, 0, "Coult not initialize gpt");
        gpt_device_finalize(device_);
        END_HELPER;
    }

    gpt_device_t* Device() {
        return device_;
    }

    const block_info_t* Info() const {
        return &block_info_;
    }

    void ReleaseGpt() {
        if (device_ != nullptr) {
            gpt_device_release(device_);
            device_ = nullptr;
        }
    }

    ~TestState() {
        ReleaseGpt();
    }

    uint64_t RootBlks() const { return blk_sz_root_; }
    uint64_t KernBlks() const { return blk_sz_kern_; }
    uint64_t RwfwBlks() const { return blk_sz_fw_; }
    uint64_t EfiBlks() const { return blk_sz_efi_; }
    uint64_t FvmBlks() const { return blk_sz_fvm_; }
    uint64_t KernCBlks() const { return blk_sz_kernc_; }
    uint64_t RootCBlks() const { return blk_sz_rootc_; }

private:
    uint64_t blk_sz_root_;
    uint64_t blk_sz_kern_;
    uint64_t blk_sz_fw_;
    uint64_t blk_sz_efi_;
    uint64_t blk_sz_fvm_;
    uint64_t blk_sz_kernc_;
    uint64_t blk_sz_rootc_;
    block_info_t block_info_;
    gpt_device_t* device_;
    fbl::unique_fd fd_;
};

typedef struct {
    uint64_t start;
    uint64_t len;
} partition_t;

bool part_size_gte(const gpt_partition_t *part, uint64_t size,
                   uint64_t block_size) {
    if (part == NULL) {
        return false;
    }
    uint64_t size_in_blocks = part->last - part->first + 1;
    return size_in_blocks * block_size >= size;
}

gpt_partition_t* find_by_type_and_name(const gpt_device_t* gpt,
                                       const uint8_t type_guid[GPT_GUID_LEN],
                                       const char *name) {
    for(size_t i = 0; i < PARTITIONS_COUNT; ++i) {
        gpt_partition_t* p = gpt->partitions[i];
        if (p == NULL) {
            continue;
        }
        char buf[GPT_NAME_LEN] = {0};
        utf16_to_cstring(&buf[0], (const uint16_t*)p->name, GPT_NAME_LEN/2);
        if(!strncmp(buf, name, GPT_NAME_LEN)) {
            return p;
        }
    }
    return NULL;
}

bool create_partition(gpt_device_t* d, const char* name, const uint8_t* type,
                      partition_t* p) {
    BEGIN_HELPER;
    uint8_t guid_buf[GPT_GUID_LEN];
    zx_cprng_draw(guid_buf, GPT_GUID_LEN);

    ASSERT_EQ(gpt_partition_add(d, name, type, guid_buf, p->start, p->len, 0),
              0, "Partition could not be added.");
    END_HELPER;
}

// create the KERN-A, KERN-B, ROOT-A, ROOT-B and state partitions
bool create_kern_roots_state(TestState* test) {
    BEGIN_HELPER;
    partition_t part_defs[5];

    // this layout is patterned off observed layouts of ChromeOS devices
    // KERN-A
    part_defs[1].start = 20480;
    part_defs[1].len = test->KernBlks();

    // ROOT-A
    part_defs[2].start = 315392;
    part_defs[2].len = test->RootBlks();

    // KERN-B
    part_defs[3].start = part_defs[1].start + part_defs[1].len;
    part_defs[3].len = test->KernBlks();

    // ROOT-B
    part_defs[4].start = part_defs[2].start + part_defs[2].len;
    part_defs[4].len = test->RootBlks();

    part_defs[0].start = part_defs[4].start + part_defs[4].len;

    gpt_device_t* device = test->Device();

    // first the rest of the disk with STATE
    uint64_t disk_start, disk_end;
    ASSERT_EQ(gpt_device_range(device, &disk_start, &disk_end), 0,
              "Retrieval of device range failed.");
    part_defs[0].len = disk_end - part_defs[0].start;

    ASSERT_TRUE(create_partition(device, "STATE", kStateGUID, &part_defs[0]));
    ASSERT_TRUE(create_partition(device, "KERN-A", kCrosKernGUID,
                                 &part_defs[1]));
    ASSERT_TRUE(create_partition(device, "ROOT-A", kCrosRootGUID,
                                 &part_defs[2]));
    ASSERT_TRUE(create_partition(device, "KERN-B", kCrosKernGUID,
                                 &part_defs[3]));
    ASSERT_TRUE(create_partition(device, "ROOT-B", kCrosRootGUID,
                                 &part_defs[4]));
    END_HELPER;
}

bool create_default_c_parts(TestState* test) {
    BEGIN_HELPER;

    gpt_device_t* device = test->Device();
    uint64_t begin, end;
    gpt_device_range(device, &begin, &end);

    partition_t part_defs[2];
    part_defs[0].start = begin;
    part_defs[0].len = kCPartsInitSize;

    part_defs[1].start = part_defs[0].start + part_defs[0].len;
    part_defs[1].len = kCPartsInitSize;

    ASSERT_TRUE(create_partition(device, "KERN-C", kCrosKernGUID,
                                 &part_defs[0]));
    ASSERT_TRUE(create_partition(device, "ROOT-C", kCrosRootGUID,
                                 &part_defs[1]));

    END_HELPER;
}

bool create_misc_parts(TestState* test) {
    BEGIN_HELPER;
    partition_t part_defs[5];
    // "OEM"
    part_defs[0].start = 86016;
    part_defs[0].len = test->KernBlks();

    // "reserved"
    part_defs[1].start = 16450;
    part_defs[1].len = 1;

    // "reserved"
    part_defs[2].start = part_defs[0].start + part_defs[0].len;
    part_defs[2].len = 1;

    // "RWFW"
    part_defs[3].start = 64;
    part_defs[3].len = test->RwfwBlks();

    // "EFI-SYSTEM"
    part_defs[4].start = 249856;
    part_defs[4].len = test->EfiBlks();

    gpt_device_t* device = test->Device();
    ASSERT_TRUE(create_partition(device, "OEM", kGenDataGUID, &part_defs[0]));
    ASSERT_TRUE(create_partition(device, "reserved", kGenDataGUID,
                                 &part_defs[1]));
    ASSERT_TRUE(create_partition(device, "reserved", kGenDataGUID,
                                 &part_defs[2]));
    ASSERT_TRUE(create_partition(device, "RWFW", kFwGUID, &part_defs[3]));
    ASSERT_TRUE(create_partition(device, "EFI-SYSTEM", kEfiGUID, &part_defs[4]));
    END_HELPER;
}

bool create_test_layout(TestState* test) {
    BEGIN_HELPER;
    ASSERT_TRUE(create_kern_roots_state(test));
    ASSERT_TRUE(create_default_c_parts(test));
    ASSERT_TRUE(create_misc_parts(test));
    END_HELPER;
}

bool add_fvm_part(TestState* test, gpt_partition_t* state) {
    BEGIN_HELPER;
    partition_t fvm_part;
    fvm_part.start = state->first;
    fvm_part.len = test->FvmBlks();
    state->first += test->FvmBlks();

    gpt_device_t* device = test->Device();
    ASSERT_TRUE(create_partition(device, "fvm", kFvmGUID, &fvm_part));

    END_HELPER;
}

void resize_kernc_from_state(TestState* test, gpt_partition_t* kernc,
                             gpt_partition_t* state) {
    kernc->first = state->first;
    kernc->last = kernc->first + test->KernCBlks() - 1;
    state->first = kernc->last + 1;
}

void resize_rootc_from_state(TestState* test, gpt_partition_t* rootc,
                             gpt_partition_t* state) {
    rootc->first = state->first;
    rootc->last = rootc->first + test->RootCBlks() - 1;
    state->first = rootc->last + 1;
}

// assumes that the base layout contains 12 partitions and that
// partition 0 is the resizable state parition
// the fvm partition will be created as the 13th partition
bool create_test_layout_with_fvm(TestState* test) {
    BEGIN_HELPER;
    ASSERT_TRUE(create_test_layout(test), "");
    ASSERT_TRUE(add_fvm_part(test, test->Device()->partitions[0]), "");
    END_HELPER;
}

bool assert_required_partitions(gpt_device_t* gpt) {
    BEGIN_HELPER;
    gpt_partition_t* part;
    part = find_by_type_and_name(gpt, kFvmGUID, "fvm");
    ASSERT_NOT_NULL(part);
    ASSERT_TRUE(part_size_gte(part, SZ_FVM_PART, BLOCK_SIZE), "FVM size");

    part = find_by_type_and_name(gpt, kCrosKernGUID, "ZIRCON-A");
    ASSERT_NOT_NULL(part);
    ASSERT_TRUE(part_size_gte(part, SZ_KERN_PART, BLOCK_SIZE), "ZIRCON-A size");

    part = find_by_type_and_name(gpt, kCrosKernGUID, "ZIRCON-B");
    ASSERT_NOT_NULL(part);
    ASSERT_TRUE(part_size_gte(part, SZ_KERN_PART, BLOCK_SIZE), "ZIRCON-B size");

    part = find_by_type_and_name(gpt, kCrosKernGUID, "ZIRCON-R");
    ASSERT_NOT_NULL(part);
    ASSERT_TRUE(part_size_gte(part, SZ_KERN_PART, BLOCK_SIZE), "ZIRCON-R size");

    part = find_by_type_and_name(gpt, kCrosKernGUID, "SYSCFG");
    ASSERT_NOT_NULL(part);
    ASSERT_TRUE(part_size_gte(part, SZ_SYSCFG_PART, BLOCK_SIZE), "SYSCFG size");
    END_HELPER;
}

bool test_default_config(void) {
    BEGIN_TEST;
    TestState test;
    ASSERT_TRUE(test.PrepareGpt());
    gpt_device_t* dev = test.Device();

    ASSERT_TRUE(create_test_layout(&test), "Test layout creation failed.");

    ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                 "Device SHOULD NOT be ready to pave.");
    ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART),
              ZX_OK, "Configuration failed.");
    ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                "Device SHOULD be ready to pave.");

    assert_required_partitions(dev);

    END_TEST;
}

bool test_already_configured(void) {
    BEGIN_TEST;
    TestState test;
    ASSERT_TRUE(test.PrepareGpt());
    gpt_device_t* dev = test.Device();

    ASSERT_TRUE(create_test_layout(&test), "Test layout creation failed.");
    ASSERT_TRUE(add_fvm_part(&test, dev->partitions[0]),
                "Could not add FVM partition record");
    resize_kernc_from_state(&test, dev->partitions[5], dev->partitions[0]);
    resize_rootc_from_state(&test, dev->partitions[6], dev->partitions[0]);

    ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                "Device SHOULD NOT be ready to pave.");

    // TODO verify that nothing changed
    ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART),
              ZX_OK, "Config failed.");

    ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                "Device SHOULD be ready to pave.");

    assert_required_partitions(dev);

    END_TEST;
}

bool test_no_c_parts(void) {
    BEGIN_TEST;
    TestState test;
    ASSERT_TRUE(test.PrepareGpt());
    gpt_device_t* dev = test.Device();

    ASSERT_TRUE(create_kern_roots_state(&test),
                "Couldn't create A/B kern and root parts");

    ASSERT_TRUE(create_misc_parts(&test), "Couldn't create misc parts");

    ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                 "Should not initially be ready to pave");

    ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART),
              ZX_OK, "Configure failed");

    ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                "Device should now be ready to pave, but isn't");

    assert_required_partitions(dev);
    END_TEST;
}

bool test_no_rootc(void) {
    BEGIN_TEST;
    TestState test;
    ASSERT_TRUE(test.PrepareGpt());
    gpt_device_t* dev = test.Device();

    ASSERT_TRUE(create_kern_roots_state(&test),
                "Couldn't make A&B kern/root parts");

    ASSERT_TRUE(create_misc_parts(&test), "Couldn't create misc parts");

    ASSERT_TRUE(create_default_c_parts(&test), "Couldn't create c parts\n");

    ASSERT_EQ(gpt_partition_remove(dev, dev->partitions[11]->guid), 0,
              "Failed to remove ROOT-C partition");

    ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                 "Should not initially be ready to pave");

    ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART),
              ZX_OK, "Configure failed");

    ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                "Device should now be ready to pave, but isn't");

    assert_required_partitions(dev);
    END_TEST;
}

bool test_no_kernc(void) {
    BEGIN_TEST;
    TestState test;
    ASSERT_TRUE(test.PrepareGpt());
    gpt_device_t* dev = test.Device();

    ASSERT_TRUE(create_kern_roots_state(&test),
                "Couldn't make A&B kern/root parts");

    ASSERT_TRUE(create_misc_parts(&test), "Couldn't create misc parts");

    ASSERT_TRUE(create_default_c_parts(&test), "Couldn't create c parts\n");

    ASSERT_EQ(gpt_partition_remove(dev, dev->partitions[10]->guid), 0,
              "Failed to remove ROOT-C partition");

    ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                 "Should not initially be ready to pave");

    ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART),
              ZX_OK, "Configure failed");

    ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                "Device should now be ready to pave, but isn't");

    assert_required_partitions(dev);

    END_TEST;
}

bool test_disk_too_small(void) {
    BEGIN_TEST;

    // first setup the device as though it is a normal test so we can compute
    // the blocks required
    TestState test;
    ASSERT_TRUE(test.PrepareGpt());
    gpt_device_t* dev = test.Device();

    ASSERT_TRUE(create_test_layout(&test), "Failed creating initial test layout");

    uint64_t reserved, unused;
    gpt_device_range(dev, &reserved, &unused);

    // this is the size we need the STATE parition to be if we are to resize
    // it to make room for the partitions we want to add and expand
    uint64_t needed_blks = howmany(SZ_ZX_PART + MIN_SZ_STATE,
                                   test.BlockSize()) + reserved;
    // now remove a few blocks so we can't satisfy all constraints
    needed_blks--;

    block_info_t info;
    memcpy(&info, &kDefaultBlockInfo, sizeof(block_info_t));
    info.block_count = dev->partitions[0]->first + needed_blks - 1;

    // now that we've calculated the block count, create a device with that
    // smaller count

    test.Initialize(info);
    ASSERT_TRUE(test.PrepareGpt());
    dev = test.Device();

    ASSERT_TRUE(create_test_layout(&test), "Failed creating real test layout");

    ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                 "Should not initially be ready to pave");

    ASSERT_NE(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART),
              ZX_OK, "Configure reported success, but should have failed.");
    ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
                 "Device should still not be paveable");
    END_TEST;
}

bool test_is_cros_device(void) {
    BEGIN_TEST;
    TestState test;
    ASSERT_TRUE(test.PrepareGpt());
    gpt_device_t* dev = test.Device();

    ASSERT_TRUE(create_test_layout(&test), "Failed to create test layout");

    ASSERT_TRUE(is_cros(dev), "This should be recognized as a chromeos layout");
    zx_cprng_draw(dev->partitions[1]->type, GPT_GUID_LEN);
    zx_cprng_draw(dev->partitions[4]->type, GPT_GUID_LEN);
    ASSERT_FALSE(is_cros(dev), "This should NOT be recognized as a chromos layout");
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(disk_wizard_tests)
RUN_TEST(test_default_config)
RUN_TEST(test_already_configured)
RUN_TEST(test_no_c_parts)
RUN_TEST(test_no_rootc)
RUN_TEST(test_no_kernc)
RUN_TEST(test_disk_too_small)
RUN_TEST(test_is_cros_device)
END_TEST_CASE(disk_wizard_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
