// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// While not much will work if launchpad isn't already working, this test
// provides a place for testing aspects of launchpad that aren't necessarily
// normally used.

#include <elfload/elfload.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <limits.h>

#include <fdio/util.h>

#include <unittest/unittest.h>

// argv[0]
static const char* program_path;

#if __has_feature(address_sanitizer)
# define LIBPREFIX "/boot/lib/asan/"
#else
# define LIBPREFIX "/boot/lib/"
#endif

static const char dynld_path[] = LIBPREFIX "ld.so.1";

static const char test_inferior_child_name[] = "inferior";

static bool launchpad_test(void)
{
    BEGIN_TEST;

    launchpad_t* lp = NULL;

    zx_handle_t fdio_job = zx_job_default();
    ASSERT_NE(fdio_job, ZX_HANDLE_INVALID, "no fdio job object");

    zx_handle_t job_copy = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_handle_duplicate(fdio_job, ZX_RIGHT_SAME_RIGHTS, &job_copy),
              ZX_OK, "zx_handle_duplicate failed");

    zx_status_t status = launchpad_create(job_copy, test_inferior_child_name, &lp);
    ASSERT_EQ(status, ZX_OK, "launchpad_create");

    zx_handle_t vmo;
    ASSERT_EQ(launchpad_vmo_from_file(program_path, &vmo), ZX_OK, "");
    status = launchpad_elf_load(lp, vmo);
    ASSERT_EQ(status, ZX_OK, "launchpad_elf_load");

    zx_vaddr_t base, entry;
    status = launchpad_get_base_address(lp, &base);
    ASSERT_EQ(status, ZX_OK, "launchpad_get_base_address");
    status = launchpad_get_entry_address(lp, &entry);
    ASSERT_EQ(status, ZX_OK, "launchpad_get_entry_address");
    ASSERT_GT(base, 0u, "base > 0");

    zx_handle_t dynld_vmo = ZX_HANDLE_INVALID;
    ASSERT_EQ(launchpad_vmo_from_file(dynld_path, &dynld_vmo), ZX_OK, "");
    ASSERT_NE(dynld_vmo, ZX_HANDLE_INVALID, "launchpad_vmo_from_file");
    elf_load_header_t header;
    uintptr_t phoff;
    status = elf_load_prepare(dynld_vmo, NULL, 0, &header, &phoff);
    ASSERT_EQ(status, ZX_OK, "elf_load_prepare");
    unittest_printf("entry %p, base %p, header entry %p\n",
                    (void*) entry, (void*) base, (void*) header.e_entry);
    ASSERT_EQ(entry, base + header.e_entry, "bad value for base or entry");
    zx_handle_close(dynld_vmo);

    launchpad_destroy(lp);

    END_TEST;
}

static bool run_one_argument_size_test(size_t size) {
    BEGIN_TEST;

    launchpad_t* lp;
    ASSERT_EQ(launchpad_create(ZX_HANDLE_INVALID, "argument size test", &lp),
              ZX_OK, "");

    char* big = malloc(size + 3);
    big[0] = ':';
    big[1] = ' ';
    memset(&big[2], 'x', size);
    big[2 + size] = '\0';
    const char* const argv[] = { "/boot/bin/sh", "-c", big };
    EXPECT_EQ(launchpad_set_args(lp, countof(argv), argv), ZX_OK, "");
    free(big);

    EXPECT_EQ(launchpad_load_from_file(lp, argv[0]), ZX_OK, "");

    zx_handle_t proc = ZX_HANDLE_INVALID;
    const char* errmsg = "???";
    EXPECT_EQ(launchpad_go(lp, &proc, &errmsg), ZX_OK, errmsg);

    EXPECT_EQ(zx_object_wait_one(proc, ZX_PROCESS_TERMINATED,
                                 ZX_TIME_INFINITE, NULL), ZX_OK, "");
    zx_info_process_t info;
    EXPECT_EQ(zx_object_get_info(proc, ZX_INFO_PROCESS,
                                 &info, sizeof(info), NULL, NULL), ZX_OK, "");
    EXPECT_EQ(zx_handle_close(proc), ZX_OK, "");

    EXPECT_EQ(info.return_code, 0, "shell exit status");

    END_TEST;
}

static bool argument_size_test(void) {
    bool ok = true;
    for (size_t size = 0; size < 2 * PAGE_SIZE; size += 1024) {
        if (!run_one_argument_size_test(size)) {
            ok = false;
            unittest_printf_critical(
                "    argument size test at %-29zu [FAILED]\n", size);
        }
    }
    return ok;
}

BEGIN_TEST_CASE(launchpad_tests)
RUN_TEST(launchpad_test);
RUN_TEST(argument_size_test);
END_TEST_CASE(launchpad_tests)

int main(int argc, char **argv)
{
    program_path = argv[0];

    bool success = unittest_run_all_tests(argc, argv);

    return success ? 0 : -1;
}
