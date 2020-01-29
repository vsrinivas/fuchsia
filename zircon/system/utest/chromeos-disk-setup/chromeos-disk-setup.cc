// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that be be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/param.h>
#include <unistd.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>

#include <memory>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <fbl/unique_fd.h>
#include <gpt/cros.h>
#include <gpt/gpt.h>
#include <unittest/unittest.h>

#define TOTAL_BLOCKS 244277248  // roughly 116GB
#define BLOCK_SIZE 512
#define SZ_FW_PART (8 * ((uint64_t)1) << 20)
#define SZ_EFI_PART (32 * ((uint64_t)1) << 20)
#define SZ_KERN_PART (16 * ((uint64_t)1) << 20)
#define SZ_FVM_PART (8 * ((uint64_t)1) << 30)
#define SZ_SYSCFG_PART (1 << 20)

namespace {

using gpt::GptDevice;
const uint8_t kStateGUID[GPT_GUID_LEN] = GUID_CROS_STATE_VALUE;
const uint8_t kCrosKernGUID[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
const uint8_t kCrosRootGUID[GPT_GUID_LEN] = GUID_CROS_ROOT_VALUE;
const uint8_t kGenDataGUID[GPT_GUID_LEN] = GUID_GEN_DATA_VALUE;
const uint8_t kFwGUID[GPT_GUID_LEN] = GUID_CROS_FIRMWARE_VALUE;
const uint8_t kEfiGUID[GPT_GUID_LEN] = GUID_EFI_VALUE;
const uint8_t kFvmGUID[GPT_GUID_LEN] = GUID_FVM_VALUE;
const uint64_t kCPartsInitSize = 1;

const fuchsia_hardware_block_BlockInfo kDefaultBlockInfo = {
    .block_count = TOTAL_BLOCKS,
    .block_size = BLOCK_SIZE,
    .max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED,
    .flags = 0,
    .reserved = 0,
};

static zx_status_t mock_read_vector(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                    zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t total = 0;
  for (size_t i = 0; i < vector_count; ++i) {
    memset(vector[i].buffer, 0, vector[i].capacity);
    total += vector[i].capacity;
  }
  *out_actual = total;
  return ZX_OK;
}

static zx_status_t mock_read_vector_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                       size_t vector_count, zxio_flags_t flags,
                                       size_t* out_actual) {
  return mock_read_vector(io, vector, vector_count, flags, out_actual);
}

static zx_status_t mock_write_vector(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                     zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t total = 0;
  for (size_t i = 0; i < vector_count; ++i) {
    total += vector[i].capacity;
  }
  *out_actual = total;
  return ZX_OK;
}

static zx_status_t mock_write_vector_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                        size_t vector_count, zxio_flags_t flags,
                                        size_t* out_actual) {
  return mock_write_vector(io, vector, vector_count, flags, out_actual);
}

static zx_status_t mock_seek(zxio_t* io, zx_off_t offset, zxio_seek_origin_t start,
                             size_t* out_offset) {
  if (start != ZXIO_SEEK_ORIGIN_START) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *out_offset = offset;
  return ZX_OK;
}

constexpr zxio_ops_t mock_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.read_vector = mock_read_vector;
  ops.read_vector_at = mock_read_vector_at;
  ops.write_vector = mock_write_vector;
  ops.write_vector_at = mock_write_vector_at;
  ops.seek = mock_seek;
  return ops;
}();

class TestState {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(TestState);

  TestState(fuchsia_hardware_block_BlockInfo info = kDefaultBlockInfo) : device_(nullptr) {
    Initialize(info);
  }

  void Initialize(fuchsia_hardware_block_BlockInfo info) {
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

  uint64_t BlockCount() const { return block_info_.block_count; }

  uint64_t BlockSize() const { return block_info_.block_size; }

  bool PrepareGpt() {
    BEGIN_HELPER;
    ASSERT_EQ(device_.get(), nullptr);

    zxio_storage_t* storage;
    fdio_t* io = fdio_zxio_create(&storage);
    ASSERT_NE(io, nullptr);
    zxio_init(&storage->io, &mock_ops);
    fd_.reset(fdio_bind_to_fd(io, -1, 0));
    ASSERT_TRUE(fd_);
    zx_status_t rc =
        GptDevice::Create(fd_.get(), static_cast<uint32_t>(BlockSize()), BlockCount(), &device_);
    ASSERT_GE(rc, 0, "Could not initialize gpt");
    ASSERT_EQ(device_->Finalize(), ZX_OK, "Could not finalize gpt");

    END_HELPER;
  }

  GptDevice* Device() { return device_.get(); }

  const fuchsia_hardware_block_BlockInfo* Info() const { return &block_info_; }

  void ReleaseGpt() {
    if (device_ != nullptr) {
      device_ = nullptr;
    }
  }

  ~TestState() { ReleaseGpt(); }

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
  fuchsia_hardware_block_BlockInfo block_info_;
  std::unique_ptr<GptDevice> device_;
  fbl::unique_fd fd_;
};

typedef struct {
  uint64_t start;
  uint64_t len;
} partition_t;

bool part_size_gte(const gpt_partition_t* part, uint64_t size, uint64_t block_size) {
  if (part == NULL) {
    return false;
  }
  uint64_t size_in_blocks = part->last - part->first + 1;
  return size_in_blocks * block_size >= size;
}

gpt_partition_t* find_by_type_and_name(const GptDevice* gpt, const uint8_t type_guid[GPT_GUID_LEN],
                                       const char* name) {
  for (uint32_t i = 0; i < gpt::kPartitionCount; ++i) {
    gpt_partition_t* p = gpt->GetPartition(i);
    if (p == NULL) {
      continue;
    }
    char buf[GPT_NAME_LEN] = {0};
    utf16_to_cstring(&buf[0], (const uint16_t*)p->name, GPT_NAME_LEN / 2);
    if (!strncmp(buf, name, GPT_NAME_LEN)) {
      return p;
    }
  }
  return NULL;
}

bool create_partition(GptDevice* d, const char* name, const uint8_t* type, partition_t* p) {
  BEGIN_HELPER;
  uint8_t guid_buf[GPT_GUID_LEN];
  zx_cprng_draw(guid_buf, GPT_GUID_LEN);

  ASSERT_EQ(d->AddPartition(name, type, guid_buf, p->start, p->len, 0), 0,
            "Partition could not be added.");
  d->Sync();
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

  GptDevice* device = test->Device();

  // first the rest of the disk with STATE
  uint64_t disk_start, disk_end;
  ASSERT_EQ(device->Range(&disk_start, &disk_end), ZX_OK, "Retrieval of device range failed.");
  part_defs[0].len = disk_end - part_defs[0].start;

  ASSERT_TRUE(create_partition(device, "STATE", kStateGUID, &part_defs[0]));
  ASSERT_TRUE(create_partition(device, "KERN-A", kCrosKernGUID, &part_defs[1]));
  ASSERT_TRUE(create_partition(device, "ROOT-A", kCrosRootGUID, &part_defs[2]));
  ASSERT_TRUE(create_partition(device, "KERN-B", kCrosKernGUID, &part_defs[3]));
  ASSERT_TRUE(create_partition(device, "ROOT-B", kCrosRootGUID, &part_defs[4]));
  END_HELPER;
}

bool create_default_c_parts(TestState* test) {
  BEGIN_HELPER;

  GptDevice* device = test->Device();
  uint64_t begin, end;
  ASSERT_EQ(device->Range(&begin, &end), ZX_OK, "Retrieval of device range failed.");

  partition_t part_defs[2];
  part_defs[0].start = begin;
  part_defs[0].len = kCPartsInitSize;

  part_defs[1].start = part_defs[0].start + part_defs[0].len;
  part_defs[1].len = kCPartsInitSize;

  ASSERT_TRUE(create_partition(device, "KERN-C", kCrosKernGUID, &part_defs[0]));
  ASSERT_TRUE(create_partition(device, "ROOT-C", kCrosRootGUID, &part_defs[1]));

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

  GptDevice* device = test->Device();
  ASSERT_TRUE(create_partition(device, "OEM", kGenDataGUID, &part_defs[0]));
  ASSERT_TRUE(create_partition(device, "reserved", kGenDataGUID, &part_defs[1]));
  ASSERT_TRUE(create_partition(device, "reserved", kGenDataGUID, &part_defs[2]));
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

  GptDevice* device = test->Device();
  ASSERT_TRUE(create_partition(device, "fvm", kFvmGUID, &fvm_part));

  END_HELPER;
}

void resize_kernc_from_state(TestState* test, gpt_partition_t* kernc, gpt_partition_t* state) {
  kernc->first = state->first;
  kernc->last = kernc->first + test->KernCBlks() - 1;
  state->first = kernc->last + 1;
}

void resize_rootc_from_state(TestState* test, gpt_partition_t* rootc, gpt_partition_t* state) {
  rootc->first = state->first;
  rootc->last = rootc->first + test->RootCBlks() - 1;
  state->first = rootc->last + 1;
}

bool assert_required_partitions(GptDevice* gpt) {
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

bool TestDefaultConfig(void) {
  BEGIN_TEST;
  TestState test;
  ASSERT_TRUE(test.PrepareGpt());
  GptDevice* dev = test.Device();

  ASSERT_TRUE(create_test_layout(&test), "Test layout creation failed.");

  ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
               "Device SHOULD NOT be ready to pave.");
  ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART), ZX_OK, "Configuration failed.");
  ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART), "Device SHOULD be ready to pave.");

  assert_required_partitions(dev);

  END_TEST;
}

bool TestAlreadyConfigured(void) {
  BEGIN_TEST;
  TestState test;
  ASSERT_TRUE(test.PrepareGpt());
  GptDevice* dev = test.Device();

  ASSERT_TRUE(create_test_layout(&test), "Test layout creation failed.");
  ASSERT_TRUE(add_fvm_part(&test, dev->GetPartition(0)), "Could not add FVM partition record");
  resize_kernc_from_state(&test, dev->GetPartition(5), dev->GetPartition(0));
  resize_rootc_from_state(&test, dev->GetPartition(6), dev->GetPartition(0));

  ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
               "Device SHOULD NOT be ready to pave.");

  // TODO verify that nothing changed
  ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART), ZX_OK, "Config failed.");

  ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART), "Device SHOULD be ready to pave.");

  assert_required_partitions(dev);

  END_TEST;
}

bool TestNoCParts(void) {
  BEGIN_TEST;
  TestState test;
  ASSERT_TRUE(test.PrepareGpt());
  GptDevice* dev = test.Device();

  ASSERT_TRUE(create_kern_roots_state(&test), "Couldn't create A/B kern and root parts");

  ASSERT_TRUE(create_misc_parts(&test), "Couldn't create misc parts");

  ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
               "Should not initially be ready to pave");

  ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART), ZX_OK, "Configure failed");

  ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
              "Device should now be ready to pave, but isn't");

  assert_required_partitions(dev);
  END_TEST;
}

bool TestNoRootc(void) {
  BEGIN_TEST;
  TestState test;
  ASSERT_TRUE(test.PrepareGpt());
  GptDevice* dev = test.Device();

  ASSERT_TRUE(create_kern_roots_state(&test), "Couldn't make A&B kern/root parts");

  ASSERT_TRUE(create_misc_parts(&test), "Couldn't create misc parts");

  ASSERT_TRUE(create_default_c_parts(&test), "Couldn't create c parts\n");

  ASSERT_EQ(dev->RemovePartition(dev->GetPartition(11)->guid), ZX_OK,
            "Failed to remove ROOT-C partition");

  ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
               "Should not initially be ready to pave");

  ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART), ZX_OK, "Configure failed");

  ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
              "Device should now be ready to pave, but isn't");

  assert_required_partitions(dev);
  END_TEST;
}

bool TestNoKernc(void) {
  BEGIN_TEST;
  TestState test;
  ASSERT_TRUE(test.PrepareGpt());
  GptDevice* dev = test.Device();

  ASSERT_TRUE(create_kern_roots_state(&test), "Couldn't make A&B kern/root parts");

  ASSERT_TRUE(create_misc_parts(&test), "Couldn't create misc parts");

  ASSERT_TRUE(create_default_c_parts(&test), "Couldn't create c parts\n");

  ASSERT_EQ(dev->RemovePartition(dev->GetPartition(10)->guid), ZX_OK,
            "Failed to remove ROOT-C partition");

  ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
               "Should not initially be ready to pave");

  ASSERT_EQ(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART), ZX_OK, "Configure failed");

  ASSERT_TRUE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
              "Device should now be ready to pave, but isn't");

  assert_required_partitions(dev);

  END_TEST;
}

bool TestDiskTooSmall(void) {
  BEGIN_TEST;

  // first setup the device as though it is a normal test so we can compute
  // the blocks required
  TestState test;
  ASSERT_TRUE(test.PrepareGpt());
  GptDevice* dev = test.Device();

  ASSERT_TRUE(create_test_layout(&test), "Failed creating initial test layout");

  uint64_t reserved, unused;
  ASSERT_EQ(dev->Range(&reserved, &unused), ZX_OK, "Retrieval of device range failed.");

  // this is the size we need the STATE partition to be if we are to resize
  // it to make room for the partitions we want to add and expand
  uint64_t needed_blks = howmany(SZ_ZX_PART + MIN_SZ_STATE, test.BlockSize()) + reserved;
  // now remove a few blocks so we can't satisfy all constraints
  needed_blks--;

  fuchsia_hardware_block_BlockInfo info;
  memcpy(&info, &kDefaultBlockInfo, sizeof(fuchsia_hardware_block_BlockInfo));
  info.block_count = dev->GetPartition(0)->first + needed_blks - 1;

  // now that we've calculated the block count, create a device with that
  // smaller count

  test.Initialize(info);
  ASSERT_TRUE(test.PrepareGpt());
  dev = test.Device();

  ASSERT_TRUE(create_test_layout(&test), "Failed creating real test layout");

  ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
               "Should not initially be ready to pave");

  ASSERT_NE(config_cros_for_fuchsia(dev, test.Info(), SZ_ZX_PART), ZX_OK,
            "Configure reported success, but should have failed.");
  ASSERT_FALSE(is_ready_to_pave(dev, test.Info(), SZ_ZX_PART),
               "Device should still not be paveable");
  END_TEST;
}

bool TestIsCrosDevice(void) {
  BEGIN_TEST;
  TestState test;
  ASSERT_TRUE(test.PrepareGpt());
  GptDevice* dev = test.Device();

  ASSERT_TRUE(create_test_layout(&test), "Failed to create test layout");

  ASSERT_TRUE(is_cros(dev), "This should be recognized as a chromeos layout");
  zx_cprng_draw(dev->GetPartition(1)->type, GPT_GUID_LEN);
  zx_cprng_draw(dev->GetPartition(4)->type, GPT_GUID_LEN);
  ASSERT_FALSE(is_cros(dev), "This should NOT be recognized as a chromos layout");
  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(disk_wizard_tests)
RUN_TEST(TestDefaultConfig)
RUN_TEST(TestAlreadyConfigured)
RUN_TEST(TestNoCParts)
RUN_TEST(TestNoRootc)
RUN_TEST(TestNoKernc)
RUN_TEST(TestDiskTooSmall)
RUN_TEST(TestIsCrosDevice)
END_TEST_CASE(disk_wizard_tests)
