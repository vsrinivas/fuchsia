// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <libzbi/zbi-cpp.h>

#include <assert.h>
#include <memory>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/auto_call.h>
#include <pretty/hexdump.h>
#include <unittest/unittest.h>
#include <zircon/boot/image.h>
#include <zircon/compiler.h>

const char kTestCmdline[] = "0123";
constexpr size_t kCmdlinePayloadLen =
    ZBI_ALIGN(static_cast<uint32_t>(sizeof(kTestCmdline)));

const char kTestRD[] = "0123456789";
constexpr size_t kRdPayloadLen =
    ZBI_ALIGN(static_cast<uint32_t>(sizeof(kTestRD)));

const char kTestBootfs[] = "abcdefghijklmnopqrs";
constexpr size_t kBootfsPayloadLen =
    ZBI_ALIGN(static_cast<uint32_t>(sizeof(kTestBootfs)));

const char kAppendRD[] = "ABCDEFG";

typedef struct test_zbi {
    // Bootdata header.
    zbi_header_t header;

    zbi_header_t cmdline_hdr;
    char cmdline_payload[kCmdlinePayloadLen];

    zbi_header_t ramdisk_hdr;
    char ramdisk_payload[kRdPayloadLen];

    zbi_header_t bootfs_hdr;
    char bootfs_payload[kBootfsPayloadLen];
} __PACKED test_zbi_t;

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
    result->header.length =
        static_cast<uint32_t>(sizeof(*result) - sizeof(zbi_header_t));
    return reinterpret_cast<uint8_t*>(result);
}

static uint8_t* get_test_zbi() {
    return get_test_zbi_extra(0);
}

static zbi_result_t check_contents(zbi_header_t* hdr, void* payload,
                                   void* cookie) {
    const char* expected = nullptr;
    const char* actual = reinterpret_cast<const char*>(payload);

    switch (hdr->type) {
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

static bool ZbiTestBasic(void) {
    BEGIN_TEST;
    uint8_t* test_zbi = get_test_zbi();

    auto cleanup = fbl::MakeAutoCall([test_zbi]() {
        free(test_zbi);
    });

    ASSERT_NONNULL(test_zbi, "failed to alloc test image");

    zbi::Zbi image(test_zbi);

    zbi_header_t* trace = nullptr;
    ASSERT_EQ(image.Check(&trace), ZBI_RESULT_OK, "malformed image");

    // zbi.Check should only give us diagnostics about the error if there was
    // an error in the first place.
    ASSERT_NULL(trace, "bad header set but image reported okay?");

    int count = 0;
    zbi_result_t result = image.ForEach(check_contents, &count);

    ASSERT_EQ(result, ZBI_RESULT_OK, "content check failed");

    ASSERT_EQ(count, 3, "bad bootdata item count");

    END_TEST;
}

static bool ZbiTestBadContainer(void) {
    BEGIN_TEST;

    uint8_t* test_zbi = get_test_zbi();

    auto cleanup = fbl::MakeAutoCall([test_zbi]() {
        free(test_zbi);
    });

    ASSERT_NONNULL(test_zbi, "failed to alloc test image");

    zbi_header_t* bootdata_header = reinterpret_cast<zbi_header_t*>(test_zbi);
    // Set to something arbitrary
    bootdata_header->type = ZBI_TYPE_STORAGE_BOOTFS;

    zbi::Zbi image(test_zbi);

    zbi_header_t* problem_header = nullptr;
    ASSERT_NE(image.Check(&problem_header), ZBI_RESULT_OK,
              "bad container fault not detected");

    // Make sure that the diagnostic information tells us that the container is
    // bad.
    ASSERT_EQ(problem_header, bootdata_header);

    END_TEST;
}

static bool ZbiTestTruncated(void) {
    BEGIN_TEST;
    uint8_t* test_zbi = get_test_zbi();

    auto cleanup = fbl::MakeAutoCall([test_zbi]() {
        free(test_zbi);
    });

    ASSERT_NONNULL(test_zbi, "failed to alloc test image");

    zbi::Zbi image(test_zbi);

    zbi_header_t* bootdata_header = reinterpret_cast<zbi_header_t*>(test_zbi);
    bootdata_header->length -= 8; // Truncate the image.

    zbi_header_t* trace = nullptr;
    ASSERT_NE(image.Check(&trace), ZBI_RESULT_OK,
              "Truncated image reported as okay");

    // zbi.Check should only give us diagnostics about the error if there was
    // an error in the first place.
    ASSERT_NONNULL(trace, "Bad image with no trace diagnostics?");

    int count = 0;
    zbi_result_t result = image.ForEach(check_contents, &count);

    ASSERT_NE(result, ZBI_RESULT_OK,
              "Truncated image not reported as truncated");

    ASSERT_EQ(count, 3, "bad bootdata item count");

    END_TEST;
}

static bool ZbiTestAppend(void) {
    BEGIN_TEST;
    // Allocate an additional kExtraBytes at the end of the ZBI to test
    // appending.
    const size_t kExtraBytes = sizeof(zbi_header_t) + sizeof(kAppendRD);
    uint8_t* test_zbi = get_test_zbi_extra(kExtraBytes);
    uint8_t* reference_zbi = get_test_zbi();

    test_zbi_t* test_image = reinterpret_cast<test_zbi_t*>(test_zbi);
    test_zbi_t* reference_image = reinterpret_cast<test_zbi_t*>(reference_zbi);

    auto cleanup = fbl::MakeAutoCall([test_zbi, reference_zbi]() {
        free(test_zbi);
        free(reference_zbi);
    });

    ASSERT_NONNULL(test_zbi, "failed to alloc test image");

    const size_t kBufferSize = sizeof(test_zbi_t) + kExtraBytes;
    zbi::Zbi image(test_zbi, kBufferSize);

    zbi_result_t result = image.AppendSection(
        static_cast<uint32_t>(sizeof(kAppendRD)), // Length
        ZBI_TYPE_STORAGE_RAMDISK,                 // Type
        0,                                        // Extra
        0,                                        // Flags
        reinterpret_cast<const void*>(kAppendRD)  // Payload.
        );

    ASSERT_EQ(result, ZBI_RESULT_OK, "Append failed");

    // Make sure the image is valid.
    ASSERT_EQ(image.Check(nullptr), ZBI_RESULT_OK,
              "append produced invalid images");

    // Verify the integrity of the data.
    reference_image->header.length = test_image->header.length;
    ASSERT_EQ(memcmp(test_zbi, reference_zbi, sizeof(test_zbi_t)), 0,
              "Append corrupted image");

    END_TEST;
}

// Make sure we never overflow the ZBI's buffer by appending.
static bool ZbiTestAppendFull(void) {
    BEGIN_TEST;

    // Enough space for a small payload
    const size_t kMaxAppendPayloadSize = ZBI_ALIGN(5);
    const size_t kExtraBytes = sizeof(zbi_header_t) + kMaxAppendPayloadSize;
    const size_t kZbiSize = sizeof(test_zbi_t) + kExtraBytes;
    const size_t kExtraSentinelLength = 64;

    uint8_t* test_zbi = get_test_zbi_extra(kExtraBytes + kExtraSentinelLength);

    ASSERT_NONNULL(test_zbi, "failed to alloc test image");

    auto cleanup = fbl::MakeAutoCall([test_zbi] {
        free(test_zbi);
    });

    // Fill the space after the buffer with sentinel bytes and make sure those
    // bytes are never touched by the append operation.
    const uint8_t kSentinelByte = 0xa5; // 0b1010 1010 0101 0101
    memset(test_zbi + kZbiSize, kSentinelByte, kExtraSentinelLength);

    zbi::Zbi image(test_zbi, kZbiSize);

    const uint8_t kDataByte = 0xc3;
    uint8_t dataBuffer[kMaxAppendPayloadSize + 1];
    memset(dataBuffer, kDataByte, kMaxAppendPayloadSize);

    // Try to append a buffer that's one byte too big and make sure we reject
    // it.
    zbi_result_t res = image.AppendSection(
        kMaxAppendPayloadSize + 1, // One more than the max length!
        ZBI_TYPE_STORAGE_RAMDISK,
        0,
        0,
        reinterpret_cast<const void*>(dataBuffer));

    ASSERT_NE(res, ZBI_RESULT_OK, "zbi appended a section that was too big");

    // Now try again with a section that is exactly the right size. Make sure
    // we don't stomp on the sentinel.
    res = image.AppendSection(
        kMaxAppendPayloadSize,
        ZBI_TYPE_STORAGE_RAMDISK,
        0,
        0,
        reinterpret_cast<const void*>(dataBuffer));

    ASSERT_EQ(res, ZBI_RESULT_OK, "zbi_append rejected a section that should "
                                  "have fit.");

    for (size_t i = 0; i < kExtraSentinelLength; i++) {
        ASSERT_EQ(test_zbi[kZbiSize + i], kSentinelByte,
                  "corrupt sentinel bytes, append section overflowed.");
    }

    END_TEST;
}

// Test that appending multiple sections to a ZBI works
static bool ZbiTestAppendMulti(void) {
    BEGIN_TEST;
    uint8_t* reference_zbi = get_test_zbi();
    ASSERT_NONNULL(reference_zbi);
    auto cleanup = fbl::MakeAutoCall([reference_zbi]() {
        free(reference_zbi);
    });

    alignas(ZBI_ALIGNMENT) uint8_t test_zbi[sizeof(test_zbi_t)];
    zbi_header_t* hdr = reinterpret_cast<zbi_header_t*>(test_zbi);

    // Create an empty container.
    init_zbi_header(hdr);
    hdr->type = ZBI_TYPE_CONTAINER;
    hdr->extra = ZBI_CONTAINER_MAGIC;
    hdr->length = 0;

    zbi::Zbi image(test_zbi, sizeof(test_zbi));

    ASSERT_EQ(image.Check(nullptr), ZBI_RESULT_OK);

    zbi_result_t result;

    result = image.AppendSection(sizeof(kTestCmdline), ZBI_TYPE_CMDLINE, 0, 0, kTestCmdline);
    ASSERT_EQ(result, ZBI_RESULT_OK);

    result = image.AppendSection(sizeof(kTestRD), ZBI_TYPE_STORAGE_RAMDISK, 0, 0, kTestRD);
    ASSERT_EQ(result, ZBI_RESULT_OK);

    result = image.AppendSection(sizeof(kTestBootfs), ZBI_TYPE_STORAGE_BOOTFS, 0, 0, kTestBootfs);
    ASSERT_EQ(result, ZBI_RESULT_OK);

    ASSERT_EQ(memcmp(reference_zbi, test_zbi, image.Length()), 0);

    END_TEST;
}

constexpr size_t kTestBufferSize = 1024;
// Test that we can initialize empty buffers as ZBI containers.
static bool ZbiTestInit(void) {
    BEGIN_TEST;

    std::unique_ptr<uint8_t[]> buffer;
    buffer.reset(new uint8_t[kTestBufferSize]);

    zbi::Zbi image(buffer.get(), kTestBufferSize);
    zbi_result_t result = image.Reset();
    ASSERT_EQ(result, ZBI_RESULT_OK);

    // Make sure that we've initialized a valid image.
    ASSERT_EQ(image.Check(nullptr), ZBI_RESULT_OK);

    result = image.AppendSection(sizeof(kTestCmdline), ZBI_TYPE_CMDLINE, 0, 0, kTestCmdline);
    ASSERT_EQ(result, ZBI_RESULT_OK);

    END_TEST;
}

// Test that we don't try to create a ZBI in a container that's not big enough.
static bool ZbiTestInitTooSmall(void) {
    BEGIN_TEST;

    constexpr uint8_t kSentinel = 0xab;

    // If all goes well, we should never write to this buffer.
    std::unique_ptr<uint8_t[]> buffer;
    buffer.reset(new uint8_t[kTestBufferSize]);

    // Write a known value into the buffer to ensure that it's not touched.
    memset(buffer.get(), kSentinel, kTestBufferSize);

    // Create a zbi that's too small to even contain a header.
    constexpr size_t kMinBufferSize = sizeof(zbi_header_t);
    zbi::Zbi image(buffer.get(), kMinBufferSize - 1);

    // Try to initialize this ZBI (should fail because there's not enough buffer)
    zbi_result_t result = image.Reset();
    EXPECT_NE(result, ZBI_RESULT_OK);

    // Make sure that the underlying buffer was never touched by libzbi.
    for (size_t i = 0; i < kTestBufferSize; i++) {
        EXPECT_EQ(buffer.get()[i], kSentinel);
    }

    END_TEST;
}

// Test the happy case.
// Make two zbi containers, extend the first by tacking the second to the back
// of it. Observe that everything went okay.
static bool ZbiTestExtendOkay(void) {
    BEGIN_TEST;

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

    END_TEST;
}

static bool ZbiTestNoOverflow(void) {
    BEGIN_TEST;
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

    ASSERT_EQ(zbi_append_section(
                  src_buffer,
                  kUsableBufferSize,
                  kUsableBufferSize - (sizeof(zbi_header_t) * 2), // Leave room for ZBI header _and_ section header
                  ZBI_TYPE_CMDLINE,
                  0, // Extra
                  0, // Flags
                  test_data),
              ZBI_RESULT_OK);

    ASSERT_EQ(zbi_extend(dst_buffer, kUsableBufferSize, src_buffer), ZBI_RESULT_OK);

    // Make sure we haven't trampled any bytes that we shouldn't have.
    for (size_t i = kUsableBufferSize; i < kUsableBufferSize; i++) {
        ASSERT_EQ(dst_buffer[i], kSentinel);
    }

    ASSERT_EQ(zbi_init(dst_buffer, kUsableBufferSize), ZBI_RESULT_OK);

    ASSERT_EQ(zbi_init(src_buffer, kUsableBufferSize + 1), ZBI_RESULT_OK);

    ASSERT_EQ(zbi_append_section(
                  src_buffer,
                  ZBI_ALIGN(kUsableBufferSize + 1),
                  (kUsableBufferSize + 1) - (sizeof(zbi_header_t) * 2), // This payload is too big.
                  ZBI_TYPE_CMDLINE,
                  0, // Extra
                  0, // Flags
                  test_data),
              ZBI_RESULT_OK);

    ASSERT_NE(zbi_extend(dst_buffer, kUsableBufferSize, src_buffer), ZBI_RESULT_OK);

    END_TEST;
}

BEGIN_TEST_CASE(zbi_tests)
// Basic tests.
RUN_TEST(ZbiTestBasic)
RUN_TEST(ZbiTestBadContainer)
RUN_TEST(ZbiTestTruncated)

// Append tests.
RUN_TEST(ZbiTestAppend)
RUN_TEST(ZbiTestAppendFull)
RUN_TEST(ZbiTestAppendMulti)

// Init tests.
RUN_TEST(ZbiTestInit)
RUN_TEST(ZbiTestInitTooSmall)

// Extend tests.
RUN_TEST(ZbiTestExtendOkay)
RUN_TEST(ZbiTestNoOverflow)
END_TEST_CASE(zbi_tests)
