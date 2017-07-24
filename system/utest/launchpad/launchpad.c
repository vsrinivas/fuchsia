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

#include <mxio/util.h>

#include <unittest/unittest.h>

// argv[0]
static const char* program_path;

static const char dynld_path[] = "/boot/lib/ld.so.1";

static const char test_inferior_child_name[] = "inferior";

static bool launchpad_test(void)
{
    BEGIN_TEST;

    launchpad_t* lp = NULL;

    mx_handle_t mxio_job = mx_job_default();
    ASSERT_GT(mxio_job, 0, "no mxio job object");

    mx_handle_t job_copy = MX_HANDLE_INVALID;
    ASSERT_EQ(mx_handle_duplicate(mxio_job, MX_RIGHT_SAME_RIGHTS, &job_copy),
              MX_OK, "mx_handle_duplicate failed");

    mx_status_t status = launchpad_create(job_copy, test_inferior_child_name, &lp);
    ASSERT_EQ(status, MX_OK, "launchpad_create");

    status = launchpad_elf_load(lp, launchpad_vmo_from_file(program_path));
    ASSERT_EQ(status, MX_OK, "launchpad_elf_load");

    mx_vaddr_t base, entry;
    status = launchpad_get_base_address(lp, &base);
    ASSERT_EQ(status, MX_OK, "launchpad_get_base_address");
    status = launchpad_get_entry_address(lp, &entry);
    ASSERT_EQ(status, MX_OK, "launchpad_get_entry_address");
    ASSERT_GT(base, 0u, "base > 0");

    mx_handle_t dynld_vmo = launchpad_vmo_from_file(dynld_path);
    ASSERT_GT(dynld_vmo, 0, "launchpad_vmo_from_file");
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

BEGIN_TEST_CASE(launchpad_tests)
RUN_TEST(launchpad_test);
END_TEST_CASE(launchpad_tests)

int main(int argc, char **argv)
{
    program_path = argv[0];

    bool success = unittest_run_all_tests(argc, argv);

    return success ? 0 : -1;
}
