// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <assert.h>
#include <lib/zbi/zbi.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/boot/image.h>
#include <zircon/compiler.h>

#include <cstring>
#include <memory>

#include <fbl/auto_call.h>
#include <pretty/hexdump.h>
#include <zxtest/zxtest.h>

const char kTestKernel[] = "4567";
constexpr size_t kKernelPayloadLen = ZBI_ALIGN(static_cast<uint32_t>(sizeof(kTestKernel)));

const char kTestCmdline[] = "0123";
constexpr size_t kCmdlinePayloadLen = ZBI_ALIGN(static_cast<uint32_t>(sizeof(kTestCmdline)));

const char kTestRD[] = "0123456789";
constexpr size_t kRdPayloadLen = ZBI_ALIGN(static_cast<uint32_t>(sizeof(kTestRD)));

const char kTestBootfs[] = "abcdefghijklmnopqrs";
constexpr size_t kBootfsPayloadLen = ZBI_ALIGN(static_cast<uint32_t>(sizeof(kTestBootfs)));

typedef struct test_zbi {
  // Bootdata header.
  zbi_header_t header;

  zbi_header_t kernel_hdr;
  char kernel_payload[kKernelPayloadLen];

  zbi_header_t cmdline_hdr;
  char cmdline_payload[kCmdlinePayloadLen];

  zbi_header_t ramdisk_hdr;
  char ramdisk_payload[kRdPayloadLen];

  zbi_header_t bootfs_hdr;
  char bootfs_payload[kBootfsPayloadLen];
} test_zbi_t;

typedef struct single_entry_test_zbi {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  zbi_header_t entry_header;
  int8_t entry_payload[8];
} single_entry_test_zbi_t;

static_assert(offsetof(test_zbi, kernel_hdr) == sizeof(test_zbi::header));
static_assert(offsetof(test_zbi, cmdline_hdr) ==
              offsetof(test_zbi, kernel_payload[kKernelPayloadLen]));
static_assert(offsetof(test_zbi, ramdisk_hdr) ==
              offsetof(test_zbi, cmdline_payload[kCmdlinePayloadLen]));
static_assert(offsetof(test_zbi, bootfs_hdr) == offsetof(test_zbi, ramdisk_payload[kRdPayloadLen]));

static_assert(sizeof(test_zbi_t) % ZBI_ALIGNMENT == 0, "");

static void init_zbi_header(zbi_header_t* hdr) {
  hdr->flags = ZBI_FLAG_VERSION;
  hdr->reserved0 = 0;
  hdr->reserved1 = 0;
  hdr->magic = ZBI_ITEM_MAGIC;
  hdr->crc32 = ZBI_ITEM_NO_CRC32;
  hdr->extra = 0;
}

static uint8_t* get_test_zbi_extra(const size_t extra_bytes) {
  const size_t kAllocSize = sizeof(test_zbi_t) + extra_bytes;
  test_zbi_t* result = reinterpret_cast<test_zbi_t*>(malloc(kAllocSize));

  if (!result) {
    return nullptr;
  }

  // Extra bytes are filled with non-zero bytes to test zero padding.
  if (extra_bytes > 0) {
    memset(result, 0xab, kAllocSize);
  }
  memset(result, 0, sizeof(*result));

  init_zbi_header(&result->header);
  result->header.type = ZBI_TYPE_CONTAINER;
  result->header.extra = ZBI_CONTAINER_MAGIC;

  init_zbi_header(&result->kernel_hdr);
#if defined(__aarch64__)
  result->kernel_hdr.type = ZBI_TYPE_KERNEL_ARM64;
#elif defined(__x86_64__) || defined(__i386__)
  result->kernel_hdr.type = ZBI_TYPE_KERNEL_X64;
#endif
  strcpy(result->kernel_payload, kTestKernel);
  result->kernel_hdr.length = static_cast<uint32_t>(sizeof(kTestKernel));

  init_zbi_header(&result->cmdline_hdr);
  result->cmdline_hdr.type = ZBI_TYPE_CMDLINE;
  strcpy(result->cmdline_payload, kTestCmdline);
  result->cmdline_hdr.length = static_cast<uint32_t>(sizeof(kTestCmdline));

  init_zbi_header(&result->ramdisk_hdr);
  result->ramdisk_hdr.type = ZBI_TYPE_STORAGE_RAMDISK;
  strcpy(result->ramdisk_payload, kTestRD);
  result->ramdisk_hdr.length = static_cast<uint32_t>(sizeof(kTestRD));

  init_zbi_header(&result->bootfs_hdr);
  result->bootfs_hdr.type = ZBI_TYPE_STORAGE_BOOTFS;
  strcpy(result->bootfs_payload, kTestBootfs);
  result->bootfs_hdr.length = static_cast<uint32_t>(sizeof(kTestBootfs));

  // The container's length is always kept aligned, though each item
  // header within the container might have an unaligned length and
  // padding bytes after that item's payload so that the following header
  // (or the end of the container) is aligned.
  result->header.length = static_cast<uint32_t>(sizeof(*result) - sizeof(zbi_header_t));
  return reinterpret_cast<uint8_t*>(result);
}

static uint8_t* get_test_zbi() { return get_test_zbi_extra(0); }

static zbi_result_t check_contents(zbi_header_t* hdr, void* payload, void* cookie) {
  const char* expected = nullptr;
  const char* actual = reinterpret_cast<const char*>(payload);

  switch (hdr->type) {
    case ZBI_TYPE_KERNEL_X64:
    case ZBI_TYPE_KERNEL_ARM64:
      expected = kTestKernel;
      break;
    case ZBI_TYPE_CMDLINE:
      expected = kTestCmdline;
      break;
    case ZBI_TYPE_STORAGE_RAMDISK:
      expected = kTestRD;
      break;
    case ZBI_TYPE_STORAGE_BOOTFS:
      expected = kTestBootfs;
      break;
    default:
      return ZBI_RESULT_ERROR;
  }

  int* itemsProcessed = reinterpret_cast<int*>(cookie);
  (*itemsProcessed)++;

  if (!strcmp(expected, actual)) {
    return ZBI_RESULT_OK;
  } else {
    return ZBI_RESULT_ERROR;
  }
}

TEST(ZbiTests, ZbiTestInit) {
  alignas(ZBI_ALIGNMENT) uint8_t buffer[sizeof(zbi_header_t)];

  ASSERT_EQ(zbi_init(buffer, sizeof(buffer)), ZBI_RESULT_OK);

  auto* zbi = reinterpret_cast<zbi_header_t*>(buffer);
  ASSERT_EQ(zbi->type, ZBI_TYPE_CONTAINER);
}

TEST(ZbiTests, ZbiTestInitTooSmall) {
  alignas(ZBI_ALIGNMENT) uint8_t buffer[sizeof(zbi_header_t) - 1];

  ASSERT_EQ(zbi_init(buffer, sizeof(buffer)), ZBI_RESULT_TOO_BIG);
}

TEST(ZbiTests, ZbiTestInitNotAligned) {
  alignas(ZBI_ALIGNMENT) uint8_t buffer[sizeof(zbi_header_t) + 1];
  void* misaligned_buffer = &buffer[1];

  ASSERT_EQ(zbi_init(misaligned_buffer, sizeof(zbi_header_t)), ZBI_RESULT_BAD_ALIGNMENT);
}

TEST(ZbiTests, ZbiTestInitNullBuffer) {
  ASSERT_EQ(zbi_init(nullptr, sizeof(zbi_header_t)), ZBI_RESULT_ERROR);
}

// TODO(fxbug.dev/52665): Consider pulling out the check logic into a common helper.
TEST(ZbiTests, ZbiTestCheckEmptyContainer) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);

  ASSERT_EQ(zbi_check(&container, nullptr), ZBI_RESULT_OK);
}

TEST(ZbiTests, ZbiTestCheckEmptyContainerWithErr) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  zbi_header_t* err = nullptr;

  EXPECT_EQ(zbi_check(&container, &err), ZBI_RESULT_OK);
  ASSERT_EQ(err, nullptr);
}

TEST(ZbiTests, ZbiTestCheckContainerBadType) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.type = 0;

  ASSERT_EQ(zbi_check(&container, nullptr), ZBI_RESULT_BAD_TYPE);
}

TEST(ZbiTests, ZbiTestCheckContainerBadTypeWithErr) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.type = 0;
  zbi_header_t* err = nullptr;

  EXPECT_EQ(zbi_check(&container, &err), ZBI_RESULT_BAD_TYPE);
  ASSERT_EQ(err, &container);
}

TEST(ZbiTests, ZbiTestCheckContainerBadExtra) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.extra = 0;

  ASSERT_EQ(zbi_check(&container, nullptr), ZBI_RESULT_BAD_MAGIC);
}

TEST(ZbiTests, ZbiTestCheckContainerBadMagic) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.magic = 0;

  ASSERT_EQ(zbi_check(&container, nullptr), ZBI_RESULT_BAD_MAGIC);
}

TEST(ZbiTests, ZbiTestCheckContainerBadVersion) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.flags &= ~ZBI_FLAG_VERSION;

  ASSERT_EQ(zbi_check(&container, nullptr), ZBI_RESULT_BAD_VERSION);
}

TEST(ZbiTests, ZbiTestCheckContainerBadCrc32) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  // Entries with no checksum must have the crc32 field set to ZBI_ITEM_NO_CRC32.
  container.flags &= ~ZBI_FLAG_CRC32;
  container.crc32 = 0;

  ASSERT_EQ(zbi_check(&container, nullptr), ZBI_RESULT_BAD_CRC);
}

TEST(ZbiTests, ZbiTestCheckTestZbi) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());

  ASSERT_EQ(zbi_check(zbi, nullptr), ZBI_RESULT_OK);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckTestZbiWithErr) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi_header_t* err = nullptr;

  EXPECT_EQ(zbi_check(zbi, &err), ZBI_RESULT_OK);
  ASSERT_EQ(err, nullptr);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckTestZbiNull) {
  ASSERT_EQ(zbi_check(nullptr, nullptr), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestCheckFirstBadEntryIsMarked) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->cmdline_hdr.magic = 0;
  zbi->ramdisk_hdr.magic = 0;
  zbi_header_t* err = nullptr;

  EXPECT_EQ(zbi_check(zbi, &err), ZBI_RESULT_BAD_MAGIC);

  ASSERT_EQ(err, &zbi->cmdline_hdr);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckTestZbiBadMagic) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->cmdline_hdr.magic = 0;

  EXPECT_EQ(zbi_check(zbi, nullptr), ZBI_RESULT_BAD_MAGIC);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckTestZbiBadMagicWithErr) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->cmdline_hdr.magic = 0;
  zbi_header_t* err = nullptr;

  EXPECT_EQ(zbi_check(zbi, &err), ZBI_RESULT_BAD_MAGIC);

  ASSERT_EQ(err, &zbi->cmdline_hdr);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckTestZbiBadVersion) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->cmdline_hdr.flags &= ~ZBI_FLAG_VERSION;

  EXPECT_EQ(zbi_check(zbi, nullptr), ZBI_RESULT_BAD_VERSION);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckTestZbiBadCrc32) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->cmdline_hdr.flags &= ~ZBI_FLAG_CRC32;
  zbi->cmdline_hdr.crc32 = 0;

  ASSERT_EQ(zbi_check(zbi, nullptr), ZBI_RESULT_BAD_CRC);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckTestZbiTruncated) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->header.length = 1;

  ASSERT_EQ(zbi_check(zbi, nullptr), ZBI_RESULT_ERR_TRUNCATED);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckCompleteTestZbi) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());

  ASSERT_EQ(zbi_check_complete(zbi, nullptr), ZBI_RESULT_OK);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckCompleteTestZbiWithErr) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi_header_t* err = nullptr;

  EXPECT_EQ(zbi_check_complete(zbi, &err), ZBI_RESULT_OK);

  ASSERT_EQ(err, nullptr);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckCompleteTestZbiNull) {
  ASSERT_EQ(zbi_check_complete(nullptr, nullptr), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestCheckCompleteTestZbiTruncated) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.length = 0;

  ASSERT_EQ(zbi_check_complete(&container, nullptr), ZBI_RESULT_ERR_TRUNCATED);
}

TEST(ZbiTests, ZbiTestCheckCompleteTestZbiWrongArch) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->kernel_hdr.type = 0;

  ASSERT_EQ(zbi_check_complete(zbi, nullptr), ZBI_RESULT_INCOMPLETE_KERNEL);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckCompleteTestZbiWrongArchWithErr) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->kernel_hdr.type = 0;
  zbi_header_t* err = nullptr;

  EXPECT_EQ(zbi_check_complete(zbi, &err), ZBI_RESULT_INCOMPLETE_KERNEL);

  ASSERT_EQ(err, &zbi->kernel_hdr);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckCompleteTestZbiMissingBootfs) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->bootfs_hdr.type = ZBI_TYPE_CMDLINE;

  ASSERT_EQ(zbi_check_complete(zbi, nullptr), ZBI_RESULT_INCOMPLETE_BOOTFS);

  free(zbi);
}

static zbi_result_t count_items_callback(zbi_header_t* header, void* payload, void* cookie) {
  *reinterpret_cast<uint32_t*>(cookie) += 1;

  return ZBI_RESULT_OK;
}

TEST(ZbiTests, ZbiTestForEachTestZbiNull) {
  ASSERT_EQ(zbi_for_each(nullptr, count_items_callback, nullptr), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestForEachTestZbiNullCallback) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);

  ASSERT_EQ(zbi_for_each(&container, nullptr, nullptr), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestForEachTestZbiContainer) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  uint32_t count = 0;

  // The callback should be invoked with ZBI items and not the container.
  EXPECT_EQ(zbi_for_each(&container, count_items_callback, &count), ZBI_RESULT_OK);
  ASSERT_EQ(count, 0);
}

TEST(ZbiTests, ZbiTestForEachTestZbiTruncated) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  // Container length does not include the size of the container header
  zbi->header.length = offsetof(test_zbi_t, cmdline_payload) - sizeof(zbi_header_t);
  uint32_t count = 0;

  // Expect the first two entries to be counted.
  EXPECT_EQ(zbi_for_each(zbi, count_items_callback, &count), ZBI_RESULT_ERR_TRUNCATED);
  ASSERT_EQ(count, 2);

  free(zbi);
}

TEST(ZbiTests, ZbiTestForEachTestZbiItems) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  uint32_t count = 0;

  EXPECT_EQ(zbi_for_each(zbi, count_items_callback, &count), ZBI_RESULT_OK);
  ASSERT_EQ(count, 4);

  free(zbi);
}

static zbi_result_t modify_payload_callback(zbi_header_t* header, void* payload, void* cookie) {
  if (cookie)
    return ZBI_RESULT_ERROR;

  std::memset(payload, 'B', 1);

  return ZBI_RESULT_OK;
}

TEST(ZbiTests, ZbiTestForEachTestZbiItemsNoCookie) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  std::memset(zbi->kernel_payload, 'A', 1);
  std::memset(zbi->cmdline_payload, 'A', 1);
  std::memset(zbi->ramdisk_payload, 'A', 1);
  std::memset(zbi->bootfs_payload, 'A', 1);

  EXPECT_EQ(zbi_for_each(zbi, modify_payload_callback, nullptr), ZBI_RESULT_OK);

  EXPECT_EQ(zbi->kernel_payload[0], 'B');
  EXPECT_EQ(zbi->cmdline_payload[0], 'B');
  EXPECT_EQ(zbi->ramdisk_payload[0], 'B');
  EXPECT_EQ(zbi->bootfs_payload[0], 'B');

  free(zbi);
}

static zbi_result_t modify_payload_then_error_callback(zbi_header_t* header, void* payload,
                                                       void* cookie) {
  auto* count = reinterpret_cast<uint32_t*>(cookie);
  if (*count > 0) {
    return ZBI_RESULT_ERROR;
  }

  std::memset(payload, 'B', 1);
  *count += 1;

  return ZBI_RESULT_OK;
}

TEST(ZbiTests, ZbiTestForEachTestZbiItemsCallbackError) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  std::memset(zbi->kernel_payload, 'A', 1);
  std::memset(zbi->cmdline_payload, 'A', 1);
  std::memset(zbi->ramdisk_payload, 'A', 1);
  std::memset(zbi->bootfs_payload, 'A', 1);
  uint32_t count = 0;

  // Only the first entry should be modified.
  EXPECT_EQ(zbi_for_each(zbi, modify_payload_then_error_callback, &count), ZBI_RESULT_ERROR);

  EXPECT_EQ(count, 1);
  EXPECT_EQ(zbi->kernel_payload[0], 'B');
  EXPECT_EQ(zbi->cmdline_payload[0], 'A');
  EXPECT_EQ(zbi->ramdisk_payload[0], 'A');
  EXPECT_EQ(zbi->bootfs_payload[0], 'A');

  free(zbi);
}

TEST(ZbiTests, ZbiTestCreateEntryTestZbi) {
  // The ZBI has space for the container and an entry with an 8-byte payload.
  single_entry_test_zbi_t zbi;
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry(&zbi, sizeof(zbi), ZBI_TYPE_CONTAINER, 0, 0, ZBI_ALIGNMENT, &payload),
            ZBI_RESULT_OK);

  // Verify the header and confirm the flag version was added.
  EXPECT_EQ(zbi.entry_header.type, ZBI_TYPE_CONTAINER);
  EXPECT_EQ(zbi.entry_header.flags & ZBI_FLAG_VERSION, ZBI_FLAG_VERSION);

  // Verify the pointer points to the newly created entry payload.
  EXPECT_EQ(payload, zbi.entry_payload);
}

TEST(ZbiTests, ZbiTestCreateEntryTestZbiNull) {
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry(nullptr, 0, 0, 0, 0, 0, &payload), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestCreateEntryTestZbiNullPayload) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);

  ASSERT_EQ(zbi_create_entry(&container, 0, 0, 0, 0, 0, nullptr), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestCreateEntryTestZbiCrc32NotSupported) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry(&container, 0, 0, 0, ZBI_FLAG_CRC32, 0, &payload), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestCreateEntryTestZbiNotContainer) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.type = 0;
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry(&container, 0, 0, 0, 0, 0, &payload), ZBI_RESULT_BAD_TYPE);
}

// create entry tests
TEST(ZbiTests, ZbiTestCreateEntryTestZbiCapacitySmallerThanCurrentSize) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.length = 2;
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry(&container, /*capacity=*/1, 0, 0, 0, 0, &payload), ZBI_RESULT_TOO_BIG);
}

TEST(ZbiTests, ZbiTestCreateEntryTestZbiFull) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry(&container, /*capacity=*/sizeof(container), 0, 0, 0,
                             /*payload_length=*/1, &payload),
            ZBI_RESULT_TOO_BIG);
}

TEST(ZbiTests, ZbiTestCreateEntryTestZbiPayloadTooLarge) {
  single_entry_test_zbi_t zbi;
  uint32_t capacity = sizeof(zbi);
  void* payload = nullptr;

  // Enough space for the entry header but not the payload.
  ASSERT_EQ(zbi_create_entry(&zbi, capacity, 0, 0, 0,
                             /*payload_length=*/capacity, &payload),
            ZBI_RESULT_TOO_BIG);
}

TEST(ZbiTests, ZbiTestCreateEntryWithPayloadTestZbi) {
  // The ZBI will have space for the container and an entry with a small payload.
  single_entry_test_zbi_t zbi;
  uint32_t payload = 0xABCDABCD;

  ASSERT_EQ(zbi_create_entry_with_payload(&zbi, sizeof(zbi), ZBI_TYPE_CONTAINER, 0, 0, &payload,
                                          sizeof(payload)),
            ZBI_RESULT_OK);

  // Verify the contents of the payload.
  ASSERT_BYTES_EQ(reinterpret_cast<uint8_t*>(zbi.entry_payload),
                  reinterpret_cast<uint8_t*>(&payload), sizeof(payload), "Mismatched payloads.");
}

TEST(ZbiTests, ZbiTestCreateEntryWithPayloadTestZbiNull) {
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry_with_payload(nullptr, 0, 0, 0, 0, &payload, 0), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestCreateEntryWithPayloadTestZbiNullPayload) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);

  ASSERT_EQ(zbi_create_entry_with_payload(&container, 0, 0, 0, 0, nullptr, 0), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestCreateEntryWithPayloadTestZbiCrc32NotSupported) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry_with_payload(&container, 0, 0, 0, ZBI_FLAG_CRC32, &payload, 0),
            ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestCreateEntryWithPayloadTestZbiNotContainer) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.type = 0;
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry_with_payload(&container, 0, 0, 0, 0, &payload, 0),
            ZBI_RESULT_BAD_TYPE);
}

TEST(ZbiTests, ZbiTestCreateEntryWithPayloadTestZbiCapacitySmallerThanCurrentSize) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  container.length = 2;
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry_with_payload(&container, /*capacity=*/1, 0, 0, 0, &payload, 0),
            ZBI_RESULT_TOO_BIG);
}

TEST(ZbiTests, ZbiTestCreateEntryWithPayloadTestZbiSectionTooLarge) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  void* payload = nullptr;

  ASSERT_EQ(zbi_create_entry_with_payload(&container, /*capacity=*/1, 0, 0, 0, &payload,
                                          /*payload_length=*/2),
            ZBI_RESULT_TOO_BIG);
}

TEST(ZbiTests, ZbiTestExtendTestZbi) {
  single_entry_test_zbi_t dst_zbi;
  single_entry_test_zbi_t src_zbi;

  uint32_t payload = 0xABCDABCD;
  ASSERT_EQ(zbi_create_entry_with_payload(&src_zbi, sizeof(src_zbi), ZBI_TYPE_CONTAINER, 0, 0,
                                          &payload, sizeof(payload)),
            ZBI_RESULT_OK);

  // Extend dst to include src's entry.
  ASSERT_EQ(zbi_extend(&dst_zbi, sizeof(dst_zbi), &src_zbi), ZBI_RESULT_OK);
  ASSERT_BYTES_EQ(reinterpret_cast<uint8_t*>(dst_zbi.entry_payload),
                  reinterpret_cast<uint8_t*>(&payload), sizeof(payload), "Mismatched payload.");
}

TEST(ZbiTests, ZbiTestExtendTestZbiDstNull) {
  zbi_header_t zbi = ZBI_CONTAINER_HEADER(0);

  ASSERT_EQ(zbi_extend(nullptr, 0, &zbi), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestExtendTestZbiSrcNull) {
  zbi_header_t zbi = ZBI_CONTAINER_HEADER(0);

  ASSERT_EQ(zbi_extend(&zbi, 0, nullptr), ZBI_RESULT_ERROR);
}

TEST(ZbiTests, ZbiTestExtendTestZbiDstNotContainer) {
  zbi_header_t src = ZBI_CONTAINER_HEADER(0);
  zbi_header_t dst = ZBI_CONTAINER_HEADER(0);
  dst.type = 0;

  ASSERT_EQ(zbi_extend(&dst, 0, &src), ZBI_RESULT_BAD_TYPE);
}

TEST(ZbiTests, ZbiTestExtendTestZbiSrcNotContainer) {
  zbi_header_t src = ZBI_CONTAINER_HEADER(0);
  src.type = 0;
  zbi_header_t dst = ZBI_CONTAINER_HEADER(0);

  ASSERT_EQ(zbi_extend(&dst, 0, &src), ZBI_RESULT_BAD_TYPE);
}

TEST(ZbiTests, ZbiTestExtendTestZbiCapacitySmallerThanDstLength) {
  zbi_header_t src = ZBI_CONTAINER_HEADER(0);
  zbi_header_t dst = ZBI_CONTAINER_HEADER(1);

  ASSERT_EQ(zbi_extend(&dst, 0, &src), ZBI_RESULT_TOO_BIG);
}

TEST(ZbiTests, ZbiTestExtendTestZbiCapacitySmallerThanDstAlignedLength) {
  zbi_header_t src = ZBI_CONTAINER_HEADER(0);
  zbi_header_t dst = ZBI_CONTAINER_HEADER(6);

  ASSERT_EQ(zbi_extend(&dst, /*capacity=*/7, &src), ZBI_RESULT_TOO_BIG);
}

TEST(ZbiTests, ZbiTestExtendTestZbiSrcTooLarge) {
  zbi_header_t src = ZBI_CONTAINER_HEADER(ZBI_ALIGNMENT + 1);
  zbi_header_t dst = ZBI_CONTAINER_HEADER(ZBI_ALIGNMENT);

  ASSERT_EQ(zbi_extend(&dst, /*capacity=*/ZBI_ALIGNMENT, &src), ZBI_RESULT_TOO_BIG);
}

// Test the happy case.
// Make two zbi containers, extend the first by tacking the second to the back
// of it. Observe that everything went okay.
TEST(ZbiTests, ZbiTestExtendOkay) {
  // Create a dst zbi that has enough space to contain the src zbi.
  uint8_t* src_buf = get_test_zbi();

  const size_t kExtraBytes = (reinterpret_cast<zbi_header_t*>(src_buf))->length;
  const size_t kDstCapacity = kExtraBytes + sizeof(test_zbi);
  uint8_t* dst_buf = get_test_zbi_extra(kExtraBytes);

  auto cleanup = fbl::MakeAutoCall([src_buf, dst_buf] {
    free(src_buf);
    free(dst_buf);
  });

  // Count the number of sections in the source buffer and the destination
  // buffer.
  uint32_t src_sections = 0;
  uint32_t dst_sections = 0;
  uint32_t combined_sections = 0;

  ASSERT_EQ(zbi_for_each(src_buf, check_contents, &src_sections), ZBI_RESULT_OK);
  ASSERT_EQ(zbi_for_each(dst_buf, check_contents, &dst_sections), ZBI_RESULT_OK);

  EXPECT_EQ(zbi_extend(dst_buf, kDstCapacity, src_buf), ZBI_RESULT_OK);

  ASSERT_EQ(zbi_for_each(dst_buf, check_contents, &combined_sections), ZBI_RESULT_OK);
  ASSERT_EQ(src_sections + dst_sections, combined_sections);
}

TEST(ZbiTests, ZbiTestNoOverflow) {
  constexpr size_t kBufferSize = 1024;
  constexpr size_t kUsableBufferSize = kBufferSize / 2;
  constexpr uint8_t kSentinel = 0xab;

  static_assert(kBufferSize % ZBI_ALIGNMENT == 0);
  static_assert(kUsableBufferSize % ZBI_ALIGNMENT == 0);

  uint8_t* dst_buffer = new uint8_t[kBufferSize];
  std::unique_ptr<uint8_t[]> dst_deleter;
  dst_deleter.reset(dst_buffer);
  memset(dst_buffer, kSentinel, kBufferSize);

  uint8_t* src_buffer = new uint8_t[kBufferSize];
  std::unique_ptr<uint8_t[]> src_deleter;
  src_deleter.reset(src_buffer);
  memset(src_buffer, kSentinel, kBufferSize);

  uint8_t* test_data = new uint8_t[kUsableBufferSize];
  std::unique_ptr<uint8_t[]> test_data_deleter;
  test_data_deleter.reset(test_data);
  memset(test_data, 0x12, kUsableBufferSize);

  ASSERT_EQ(zbi_init(dst_buffer, kUsableBufferSize), ZBI_RESULT_OK);
  ASSERT_EQ(zbi_init(src_buffer, kUsableBufferSize), ZBI_RESULT_OK);

  ASSERT_EQ(zbi_create_entry_with_payload(
                src_buffer, kUsableBufferSize, ZBI_TYPE_CMDLINE,
                0,  // Extra
                0,  // Flags
                test_data,
                kUsableBufferSize -
                    (sizeof(zbi_header_t) * 2)  // Leave room for ZBI header _and_ section header
                ),
            ZBI_RESULT_OK);

  ASSERT_EQ(zbi_extend(dst_buffer, kUsableBufferSize, src_buffer), ZBI_RESULT_OK);

  // Make sure we haven't trampled any bytes that we shouldn't have.
  for (size_t i = kUsableBufferSize; i < kUsableBufferSize; i++) {
    ASSERT_EQ(dst_buffer[i], kSentinel);
  }

  ASSERT_EQ(zbi_init(dst_buffer, kUsableBufferSize), ZBI_RESULT_OK);

  ASSERT_EQ(zbi_init(src_buffer, kUsableBufferSize + 1), ZBI_RESULT_OK);

  ASSERT_EQ(zbi_create_entry_with_payload(
                src_buffer, ZBI_ALIGN(kUsableBufferSize + 1), ZBI_TYPE_CMDLINE,
                0,  // Extra
                0,  // Flags
                test_data,
                (kUsableBufferSize + 1) - (sizeof(zbi_header_t) * 2)  // This payload is too big.
                ),
            ZBI_RESULT_OK);

  ASSERT_NE(zbi_extend(dst_buffer, kUsableBufferSize, src_buffer), ZBI_RESULT_OK);
}
