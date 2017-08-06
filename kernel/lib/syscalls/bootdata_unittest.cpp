// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(MG-1014): This file should be moved into a kernel library for processing
//                bootdata along with all the other bootdata processing code.
//                It lives here temorarily until such a library exists because
//                the method that it tests lives inside syscalls_system.cpp

#include <assert.h>
#include <magenta/boot/bootdata.h>
#include <magenta/types.h>
#include <mexec.h>
#include <stddef.h>
#include <unittest.h>

const size_t kBootdataSentinelLen = 64;
const uint8_t kBootdataSentinelByte = 0xA5;
const uint8_t kBootdataSectionContentsByte = 0x99;

static void init_bootdata(uint8_t* bd, const size_t len) {
    DEBUG_ASSERT(len >= sizeof(bootdata_t));

    bootdata_t* hdr = (bootdata_t*)bd;
    hdr->type = BOOTDATA_CONTAINER;
    hdr->extra = BOOTDATA_MAGIC;
    hdr->flags = 0;
    hdr->length = 0;
}

static bool sentinel_integrity_okay(const uint8_t* sentinel, const size_t len) {
    bool sentinel_okay = true;
    for (size_t i = 0; i < len; i++) {
        if (sentinel[i] != kBootdataSentinelByte) {
            sentinel_okay = false;
            break;
        }
    }
    return sentinel_okay;
}

// Try to overflow the bootdata buffer.
static bool bootdata_overflow_test(void* context) {
    BEGIN_TEST;

    const size_t kBootdataBufferLen = 64;

    uint8_t bootdata_buffer[kBootdataBufferLen + kBootdataSentinelLen];
    uint8_t* bootdata_sentinel = bootdata_buffer + kBootdataBufferLen;
    memset(bootdata_buffer, kBootdataSentinelByte, sizeof(bootdata_buffer));

    init_bootdata(bootdata_buffer, kBootdataBufferLen);

    // Deliberately attempt to overflow the bootdata.
    uint8_t bootdata_section[kBootdataBufferLen + 1];
    mx_status_t result = bootdata_append_section(bootdata_buffer, kBootdataBufferLen,
                                                 bootdata_section, sizeof(bootdata_section),
                                                 0x0, 0x0, 0x0);

    EXPECT_EQ(result, MX_ERR_BUFFER_TOO_SMALL, "append boot section");

    // Make sure that we didn't touch any of the overflow buffer.
    EXPECT_TRUE(sentinel_integrity_okay(bootdata_sentinel, kBootdataSentinelLen),
                "check sentinel integrity");

    END_TEST;
}

// Fill the entire boot
static bool bootdata_fill_test(void* context) {
    BEGIN_TEST;

    // The bootdata buffer the Global Bootdata Header + The Section Header + the Section Contents
    const size_t kSectionSize = BOOTDATA_ALIGN(64);
    const size_t kBootdataBufferLen = 2 * sizeof(bootdata_t) + kSectionSize;
    uint8_t bootdata_buffer[kBootdataBufferLen + kBootdataSentinelLen];

    // Setup the sentinel to contain the sentinel byte.
    uint8_t* bootdata_sentinel = bootdata_buffer + kBootdataBufferLen;
    memset(bootdata_buffer, kBootdataSentinelByte, sizeof(bootdata_buffer));

    init_bootdata(bootdata_buffer, kBootdataBufferLen);

    // Attempt to fill the whole bootdata.
    uint8_t bootdata_section[kSectionSize];
    memset(bootdata_section, kBootdataSectionContentsByte, kSectionSize);

    mx_status_t result = bootdata_append_section(bootdata_buffer, kBootdataBufferLen,
                                                 bootdata_section, kSectionSize,
                                                 0x0, 0x0, 0x0);

    EXPECT_EQ(MX_OK, result, "fill whole bootdata buffer");

    EXPECT_TRUE(sentinel_integrity_okay(bootdata_sentinel, kBootdataSentinelLen),
                "check sentinel integrity");

    END_TEST;
}

// Use the function name as the test name
#define BOOTDATA_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(bootdata_tests)
BOOTDATA_UNITTEST(bootdata_overflow_test)
BOOTDATA_UNITTEST(bootdata_fill_test)

UNITTEST_END_TESTCASE(bootdata_tests, "bootdata", "bootdata packing tests", nullptr, nullptr);