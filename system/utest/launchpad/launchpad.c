// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// While not much will work if launchpad isn't already working, this test
// provides a place for testing aspects of launchpad that aren't necessarily
// normally used.

#include <elfload/elfload.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <limits.h>

#include <mxio/util.h>

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

    mx_handle_t mxio_job = mx_job_default();
    ASSERT_NE(mxio_job, MX_HANDLE_INVALID, "no mxio job object");

    mx_handle_t job_copy = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_handle_duplicate(mxio_job, MX_RIGHT_SAME_RIGHTS, &job_copy),
              MX_OK, "mx_handle_duplicate failed");

    mx_status_t status = launchpad_create(job_copy, test_inferior_child_name, &lp);
    ASSERT_EQ(status, MX_OK, "launchpad_create");

    mx_handle_t vmo;
    ASSERT_EQ(launchpad_vmo_from_file(program_path, &vmo), MX_OK, "");
    status = launchpad_elf_load(lp, vmo);
    ASSERT_EQ(status, MX_OK, "launchpad_elf_load");

    mx_vaddr_t base, entry;
    status = launchpad_get_base_address(lp, &base);
    ASSERT_EQ(status, MX_OK, "launchpad_get_base_address");
    status = launchpad_get_entry_address(lp, &entry);
    ASSERT_EQ(status, MX_OK, "launchpad_get_entry_address");
    ASSERT_GT(base, 0u, "base > 0");

    mx_handle_t dynld_vmo = MX_HANDLE_INVALID;
    ASSERT_EQ(launchpad_vmo_from_file(dynld_path, &dynld_vmo), MX_OK, "");
    ASSERT_NE(dynld_vmo, MX_HANDLE_INVALID, "launchpad_vmo_from_file");
    elf_load_header_t header;
    uintptr_t phoff;
    status = elf_load_prepare(dynld_vmo, NULL, 0, &header, &phoff);
    ASSERT_EQ(status, MX_OK, "elf_load_prepare");
    unittest_printf("entry %p, base %p, header entry %p\n",
                    (void*) entry, (void*) base, (void*) header.e_entry);
    ASSERT_EQ(entry, base + header.e_entry, "bad value for base or entry");
    mx_handle_close(dynld_vmo);

    launchpad_destroy(lp);

    END_TEST;
}

static bool run_one_argument_size_test(size_t size) {
    BEGIN_TEST;

    launchpad_t* lp;
    ASSERT_EQ(launchpad_create(MX_HANDLE_INVALID, "argument size test", &lp),
              MX_OK, "");

    char* big = malloc(size + 3);
    big[0] = ':';
    big[1] = ' ';
    memset(&big[2], 'x', size);
    big[2 + size] = '\0';
    const char* const argv[] = { "/boot/bin/sh", "-c", big };
    EXPECT_EQ(launchpad_set_args(lp, countof(argv), argv), MX_OK, "");
    free(big);

    EXPECT_EQ(launchpad_load_from_file(lp, argv[0]), MX_OK, "");

    mx_handle_t proc = MX_HANDLE_INVALID;
    const char* errmsg = "???";
    EXPECT_EQ(launchpad_go(lp, &proc, &errmsg), MX_OK, errmsg);

    EXPECT_EQ(mx_object_wait_one(proc, MX_PROCESS_TERMINATED,
                                 MX_TIME_INFINITE, NULL), MX_OK, "");
    mx_info_process_t info;
    EXPECT_EQ(mx_object_get_info(proc, MX_INFO_PROCESS,
                                 &info, sizeof(info), NULL, NULL), MX_OK, "");
    EXPECT_EQ(mx_handle_close(proc), MX_OK, "");

    EXPECT_EQ(info.return_code, 0, "shell exit status");

    END_TEST;
}

static bool argument_size_test(void) {
    bool ok = true;
    for (size_t size = 0; size < 2 * PAGE_SIZE; size += 16) {
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
