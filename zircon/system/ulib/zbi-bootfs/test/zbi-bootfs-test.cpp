// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cerrno>
#include <fcntl.h>
#include <getopt.h>
#include <string>

#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <lib/fzl/fdio.h>
#include <lib/zx/vmo.h>

#include <unittest/unittest.h>
#include <zbi-bootfs/zbi-bootfs.h>

constexpr char kFilePath[] = "boot/testdata/zbi-bootfs/test-image.zbi";
constexpr char kFileName[] = "nand_image";

static bool ZbiInit(void) {
    BEGIN_TEST;
    zbi_bootfs::ZbiBootfsParser image;
    size_t byte_offset = 0;
    const char* input = kFilePath;

    // Check good input
    zx::vmo vmo_out;
    ASSERT_EQ(ZX_OK, image.Init(input, byte_offset));

    END_TEST;
}

static bool ZbiInitBadInput(void) {
    BEGIN_TEST;
    zbi_bootfs::ZbiBootfsParser image;

    // Check bad input
    const char* input = nullptr;
    size_t byte_offset = 0;
    ASSERT_EQ(ZX_ERR_IO, image.Init(input, byte_offset));

    END_TEST;
}

static bool ZbiProcessSuccess(void) {
    BEGIN_TEST;
    zbi_bootfs::ZbiBootfsParser image;
    const char* input = kFilePath;
    const char* filename = kFileName;
    size_t byte_offset = 0;

    zbi_bootfs::Entry entry;

    ASSERT_EQ(ZX_OK, image.Init(input, byte_offset));

    // Check bootfs filename
    // This will return a list of Bootfs entires, plus details of "filename" entry
    ASSERT_EQ(ZX_OK, image.ProcessZbi(filename, &entry));
    END_TEST;
}

static bool ZbiProcessBadOffset(void) {
    BEGIN_TEST;
    zbi_bootfs::ZbiBootfsParser image;
    const char* input = kFilePath;
    const char* filename = kFileName;
    zbi_bootfs::Entry entry;

    // Check loading zbi with bad offset value and then try processing it
    // This should return an error
    size_t byte_offset = 1;
    ASSERT_EQ(ZX_OK, image.Init(input, byte_offset));
    ASSERT_EQ(ZX_ERR_BAD_STATE, image.ProcessZbi(filename, &entry));

    END_TEST;
}

static bool ZbiProcessBadFile(void) {
    BEGIN_TEST;
    zbi_bootfs::ZbiBootfsParser image;
    const char* input = kFilePath;
    size_t byte_offset = 0;
    zbi_bootfs::Entry entry;

    ASSERT_EQ(ZX_OK, image.Init(input, byte_offset));
    // Check bad payload filename
    // This will return a list of payload (Bootfs) entires
    const char* filename = "";
    ASSERT_EQ(ZX_ERR_NOT_FOUND, image.ProcessZbi(filename, &entry));

    END_TEST;
}

BEGIN_TEST_CASE(zbi_tests)

RUN_TEST(ZbiInit)
RUN_TEST(ZbiInitBadInput)
RUN_TEST(ZbiProcessSuccess)
RUN_TEST(ZbiProcessBadOffset)
RUN_TEST(ZbiProcessBadFile)

END_TEST_CASE(zbi_tests)
