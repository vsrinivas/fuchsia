// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "zbi.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/boot/image.h>
#include <zircon/compiler.h>

#include <cstring>

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
  hdr->flags = ZBI_FLAGS_VERSION;
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
  container.flags &= ~ZBI_FLAGS_VERSION;

  ASSERT_EQ(zbi_check(&container, nullptr), ZBI_RESULT_BAD_VERSION);
}

TEST(ZbiTests, ZbiTestCheckContainerBadCrc32) {
  zbi_header_t container = ZBI_CONTAINER_HEADER(0);
  // Entries with no checksum must have the crc32 field set to ZBI_ITEM_NO_CRC32.
  container.flags &= ~ZBI_FLAGS_CRC32;
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
  zbi->cmdline_hdr.flags &= ~ZBI_FLAGS_VERSION;

  EXPECT_EQ(zbi_check(zbi, nullptr), ZBI_RESULT_BAD_VERSION);

  free(zbi);
}

TEST(ZbiTests, ZbiTestCheckTestZbiBadCrc32) {
  test_zbi_t* zbi = reinterpret_cast<test_zbi_t*>(get_test_zbi());
  zbi->cmdline_hdr.flags &= ~ZBI_FLAGS_CRC32;
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
  EXPECT_EQ(zbi.entry_header.flags & ZBI_FLAGS_VERSION, ZBI_FLAGS_VERSION);

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

  ASSERT_EQ(zbi_create_entry(&container, 0, 0, 0, ZBI_FLAGS_CRC32, 0, &payload), ZBI_RESULT_ERROR);
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

  ASSERT_EQ(zbi_create_entry_with_payload(&container, 0, 0, 0, ZBI_FLAGS_CRC32, &payload, 0),
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
