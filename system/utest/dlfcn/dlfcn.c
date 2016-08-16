// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/vmo.h>
#include <magenta/dlfcn.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>
#include <stdio.h>
#include <string.h>

#include <unittest/unittest.h>

bool dlopen_vmo_test(void) {
    BEGIN_TEST;

    mx_handle_t vmo = launchpad_vmo_from_file("/boot/lib/liblaunchpad.so");
    EXPECT_GT(vmo, 0, "launchpad_vmo_from_file");

    void* obj = dlopen_vmo(vmo, RTLD_LOCAL);
    EXPECT_NEQ(obj, NULL, "dlopen_vmo");

    mx_handle_close(vmo);

    void* sym = dlsym(obj, "launchpad_create");
    EXPECT_NEQ(sym, NULL, "dlsym");

    int ok = dlclose(obj);
    EXPECT_EQ(ok, 0, "dlclose");

    END_TEST;
}

// This should be some library that this program links against.
#define TEST_SONAME "libmxio.so"
#define TEST_NAME "foobar"
#define TEST_ACTUAL_NAME "/boot/lib/" TEST_SONAME

static bool my_loader_service_ok = false;
static int my_loader_service_calls;

static mx_handle_t my_loader_service(void* arg, const char* name) {
    ++my_loader_service_calls;

    bool all_ok = true;

    EXPECT_EQ(strcmp(name, TEST_NAME), 0, "called with unexpected name");

    mx_handle_t vmo = launchpad_vmo_from_file(arg);
    EXPECT_GT(vmo, 0, "launchpad_vmo_from_file");

    my_loader_service_ok = all_ok;
    return vmo;
}

static void show_dlerror(void) {
    unittest_printf_critical("dlerror: %s\n", dlerror());
}

bool loader_service_test(void) {
    BEGIN_TEST;

    // Get a handle to an existing library with a known SONAME.
    void* by_name = dlopen(TEST_SONAME, RTLD_NOLOAD);
    EXPECT_NEQ(by_name, NULL, "dlopen failed on " TEST_SONAME);
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

    EXPECT_NEQ(via_service, NULL, "dlopen via service");
    if (via_service == NULL)
        show_dlerror();

    EXPECT_TRUE(my_loader_service_ok, "loader service thread not happy");

    // It should not just have succeeded, but gotten the very
    // same handle as the by-name lookup.
    EXPECT_EQ(via_service, by_name, "dlopen via service");

    int fail = dlclose(by_name);
    EXPECT_EQ(fail, 0, "dlclose on by-name");
    if (fail)
        show_dlerror();

    fail = dlclose(via_service);
    EXPECT_EQ(fail, 0, "dlclose on via-service");
    if (fail)
        show_dlerror();

    END_TEST;
}

BEGIN_TEST_CASE(dlfcn_tests)
RUN_TEST(dlopen_vmo_test);
RUN_TEST(loader_service_test);
END_TEST_CASE(dlfcn_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
