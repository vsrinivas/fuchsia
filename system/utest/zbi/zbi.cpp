// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <zircon/boot/image.h>
#include <zircon/compiler.h>
#include <zbi/zbi-cpp.h>
#include <unittest/unittest.h>
#include <fbl/auto_call.h>

const char* kTestCmdline = "0123";
const char* kTestRD = "0123456789";
const char* kTestBootfs = "abcdefghijklmnopqrs";

typedef struct test_zbi {
    // Bootdata header.
    zbi_header_t header;

    zbi_header_t cmdline_hdr;
    char cmdline_payload[ZBI_ALIGNMENT];

    zbi_header_t ramdisk_hdr;
    char ramdisk_payload[ZBI_ALIGNMENT * 2];

    zbi_header_t bootfs_hdr;
    char bootfs_payload[ZBI_ALIGNMENT * 3];
} __PACKED test_zbi_t;

static void init_zbi_header(zbi_header_t* hdr) {
    hdr->flags = ZBI_FLAG_VERSION;
    hdr->reserved0 = 0;
    hdr->reserved1 = 0;
    hdr->magic = ZBI_ITEM_MAGIC;
    hdr->crc32 = ZBI_ITEM_NO_CRC32;
    hdr->extra = 0;
}

static uint8_t* get_test_zbi() {
    test_zbi_t* result = reinterpret_cast<test_zbi_t*>(malloc(sizeof(*result)));

    if (!result) return nullptr;

    init_zbi_header(&result->header);
    result->header.type = ZBI_TYPE_CONTAINER;
    result->header.extra = ZBI_CONTAINER_MAGIC;

    init_zbi_header(&result->cmdline_hdr);
    result->cmdline_hdr.type = ZBI_TYPE_CMDLINE;
    strcpy(result->cmdline_payload, kTestCmdline);
    result->cmdline_hdr.length =
        static_cast<uint32_t>(strlen(result->cmdline_payload));

    init_zbi_header(&result->ramdisk_hdr);
    result->ramdisk_hdr.type = ZBI_TYPE_STORAGE_RAMDISK;
    strcpy(result->ramdisk_payload, kTestRD);
    result->ramdisk_hdr.length =
        static_cast<uint32_t>(strlen(result->ramdisk_payload));

    init_zbi_header(&result->bootfs_hdr);
    result->bootfs_hdr.type = ZBI_TYPE_STORAGE_BOOTFS;
    strcpy(result->bootfs_payload, kTestBootfs);
    result->bootfs_hdr.length =
        static_cast<uint32_t>(strlen(result->bootfs_payload));

    // We have to be a little bit careful about setting the length of the
    // container itself since its length should not contain the padding of the
    // last boot item but will likely contain the padding of all internal
    // boot items.
    result->header.length = static_cast<uint32_t>(
        sizeof(*result)
        - sizeof(result->header)          // Don't count container header.
        - sizeof(result->bootfs_payload)  // Subtract the last entry...
        + strlen(result->bootfs_payload)  // ... and add back non-padding bytes.
    );
    return reinterpret_cast<uint8_t*>(result);
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

static bool zbi_test_basic(void) {
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

static bool zbi_test_bad_container(void) {
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

static bool zbi_test_truncated(void) {
    BEGIN_TEST;
    uint8_t* test_zbi = get_test_zbi();

    auto cleanup = fbl::MakeAutoCall([test_zbi]() {
        free(test_zbi);
    });

    ASSERT_NONNULL(test_zbi, "failed to alloc test image");

    zbi::Zbi image(test_zbi);

    zbi_header_t* bootdata_header = reinterpret_cast<zbi_header_t*>(test_zbi);
    bootdata_header->length -= 1;   // Truncate the image.

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


BEGIN_TEST_CASE(zbi_tests)
RUN_TEST(zbi_test_basic)
RUN_TEST(zbi_test_bad_container)
RUN_TEST(zbi_test_truncated)
END_TEST_CASE(zbi_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
