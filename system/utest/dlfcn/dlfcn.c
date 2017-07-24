// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <launchpad/vmo.h>
#include <magenta/device/dmctl.h>
#include <magenta/dlfcn.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/loader-service.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <unittest/unittest.h>

bool dlopen_vmo_test(void) {
    BEGIN_TEST;

    mx_handle_t vmo = launchpad_vmo_from_file("/boot/lib/liblaunchpad.so");
    EXPECT_GT(vmo, 0, "launchpad_vmo_from_file");

    void* obj = dlopen_vmo(vmo, RTLD_LOCAL);
    EXPECT_NONNULL(obj, "dlopen_vmo");

    mx_handle_close(vmo);

    void* sym = dlsym(obj, "launchpad_create");
    EXPECT_NONNULL(sym, "dlsym");

    int ok = dlclose(obj);
    EXPECT_EQ(ok, 0, "dlclose");

    END_TEST;
}

// This should be some library that this program links against.
#define TEST_SONAME "libmxio.so"
#define TEST_NAME "foobar"
#define TEST_ACTUAL_NAME "/boot/lib/" TEST_SONAME

static atomic_bool my_loader_service_ok = false;
static atomic_int my_loader_service_calls = 0;

static mx_handle_t my_loader_service(void* arg, uint32_t load_op,
                                     mx_handle_t request_handle,
                                     const char* name) {
    ++my_loader_service_calls;

    EXPECT_EQ(request_handle, MX_HANDLE_INVALID,
              "called with a request handle");

    int cmp = strcmp(name, TEST_NAME);
    EXPECT_EQ(cmp, 0, "called with unexpected name");
    if (cmp != 0) {
        unittest_printf("        saw \"%s\", expected \"%s\"", name, TEST_NAME);
        return MX_HANDLE_INVALID;
    }
    EXPECT_EQ(load_op, (uint32_t) LOADER_SVC_OP_LOAD_OBJECT,
              "called with unexpected load op");
    if (load_op != (uint32_t) LOADER_SVC_OP_LOAD_OBJECT) {
        unittest_printf("        saw %" PRIu32 ", expected %" PRIu32, load_op,
                        LOADER_SVC_OP_LOAD_OBJECT);
        return MX_HANDLE_INVALID;
    }

    mx_handle_t vmo = launchpad_vmo_from_file(arg);
    EXPECT_GT(vmo, 0, "launchpad_vmo_from_file");
    if (vmo <= 0) {
        return vmo;
    }

    my_loader_service_ok = true;
    return vmo;
}

static void show_dlerror(void) {
    unittest_printf_critical("dlerror: %s\n", dlerror());
}

bool loader_service_test(void) {
    BEGIN_TEST;

    // Get a handle to an existing library with a known SONAME.
    void* by_name = dlopen(TEST_SONAME, RTLD_NOLOAD);
    EXPECT_NONNULL(by_name, "dlopen failed on " TEST_SONAME);
    if (by_name == NULL)
        show_dlerror();

    // Spin up our test service.
    mx_handle_t my_service =
        mxio_loader_service(&my_loader_service, (void*)TEST_ACTUAL_NAME);
    EXPECT_GT(my_service, 0, "mxio_loader_service");

    // Install the service.
    mx_handle_t old = dl_set_loader_service(my_service);
    EXPECT_GT(old, 0, "dl_set_loader_service");

    // Now to a lookup that should go through our service.  It
    // should load up the new copy of the file, find that its
    // SONAME matches an existing library, and just return it.
    void *via_service = dlopen(TEST_NAME, RTLD_LOCAL);

    EXPECT_EQ(my_loader_service_calls, 1,
              "loader-service not called exactly once");

    EXPECT_NONNULL(via_service, "dlopen via service");
    if (via_service == NULL)
        show_dlerror();

    EXPECT_TRUE(my_loader_service_ok, "loader service thread not happy");

    // It should not just have succeeded, but gotten the very
    // same handle as the by-name lookup.
    EXPECT_TRUE(via_service == by_name, "dlopen via service");

    int fail = dlclose(by_name);
    EXPECT_EQ(fail, 0, "dlclose on by-name");
    if (fail)
        show_dlerror();

    fail = dlclose(via_service);
    EXPECT_EQ(fail, 0, "dlclose on via-service");
    if (fail)
        show_dlerror();

    // Put things back to how they were.
    mx_handle_t old2 = dl_set_loader_service(old);
    EXPECT_EQ(old2, my_service, "unexpected previous service handle");
    mx_handle_close(old2);

    END_TEST;
}

#define DMCTL_PATH "/dev/misc/dmctl"

bool ioctl_test(void) {
    BEGIN_TEST;

    int fd = open(DMCTL_PATH, O_RDONLY);
    ASSERT_GE(fd, 0, "can't open " DMCTL_PATH);

    mx_handle_t h;
    ssize_t s = ioctl_dmctl_get_loader_service_channel(fd, &h);
    close(fd);

    EXPECT_EQ(s, (ssize_t)sizeof(mx_handle_t),
              "unexpected return value from ioctl");
    EXPECT_GT(h, 0, "invalid handle from ioctl");

    mx_handle_close(h);

    END_TEST;
}

// TODO(dbort): Test that this process uses the system loader service by default

BEGIN_TEST_CASE(dlfcn_tests)
RUN_TEST(dlopen_vmo_test);
RUN_TEST(loader_service_test);
RUN_TEST(ioctl_test);
END_TEST_CASE(dlfcn_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
