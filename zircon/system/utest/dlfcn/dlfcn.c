// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <inttypes.h>
#include <launchpad/vmo.h>
#include <loader-service/loader-service.h>
#include <ldmsg/ldmsg.h>
#include <zircon/dlfcn.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <unittest/unittest.h>

#if __has_feature(address_sanitizer)
# define LIBPREFIX "/boot/lib/asan/"
#else
# define LIBPREFIX "/boot/lib/"
#endif

bool dlopen_vmo_test(void) {
    BEGIN_TEST;

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    zx_status_t status = launchpad_vmo_from_file(LIBPREFIX "liblaunchpad.so", &vmo);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_NE(vmo, ZX_HANDLE_INVALID, "launchpad_vmo_from_file");

    void* obj = dlopen_vmo(vmo, RTLD_LOCAL);
    EXPECT_NONNULL(obj, "dlopen_vmo");

    zx_handle_close(vmo);

    void* sym = dlsym(obj, "launchpad_create");
    EXPECT_NONNULL(sym, "dlsym");

    int ok = dlclose(obj);
    EXPECT_EQ(ok, 0, "dlclose");

    END_TEST;
}

// This should be some library that this program links against.
#define TEST_SONAME "libfdio.so"
#define TEST_NAME "foobar"
#define TEST_ACTUAL_NAME LIBPREFIX TEST_SONAME

static atomic_bool my_loader_service_ok = false;
static atomic_int my_loader_service_calls = 0;

static zx_status_t my_load_object(void* ctx, const char* name, zx_handle_t* out) {
    ++my_loader_service_calls;

    int cmp = strcmp(name, TEST_NAME);
    EXPECT_EQ(cmp, 0, "called with unexpected name");
    if (cmp != 0) {
        unittest_printf("        saw \"%s\", expected \"%s\"", name, TEST_NAME);
        return ZX_HANDLE_INVALID;
    }

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    zx_status_t status = launchpad_vmo_from_file((char*) ctx, &vmo);
    EXPECT_EQ(status, ZX_OK, "");
    EXPECT_NE(vmo, ZX_HANDLE_INVALID, "launchpad_vmo_from_file");
    if (status < 0) {
        return status;
    }

    my_loader_service_ok = true;
    *out = vmo;
    return ZX_OK;
}

static zx_status_t my_load_abspath(void* ctx, const char* name, zx_handle_t* vmo) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t my_publish_data_sink(void* ctx, const char* name, zx_handle_t vmo) {
    zx_handle_close(vmo);
    return ZX_ERR_NOT_SUPPORTED;
}

static loader_service_ops_t my_loader_ops = {
    .load_object = my_load_object,
    .load_abspath = my_load_abspath,
    .publish_data_sink = my_publish_data_sink,
};

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
    loader_service_t* svc = NULL;
    zx_status_t status = loader_service_create(NULL, &my_loader_ops, (void*)TEST_ACTUAL_NAME, &svc);
    EXPECT_EQ(status, ZX_OK, "loader_service_create");

    zx_handle_t my_service = ZX_HANDLE_INVALID;
    status = loader_service_connect(svc, &my_service);
    EXPECT_EQ(status, ZX_OK, "loader_service_connect");

    // Install the service.
    zx_handle_t old = dl_set_loader_service(my_service);
    EXPECT_NE(old, ZX_HANDLE_INVALID, "dl_set_loader_service");

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
    zx_handle_t old2 = dl_set_loader_service(old);
    EXPECT_EQ(old2, my_service, "unexpected previous service handle");
    zx_handle_close(old2);

    END_TEST;
}

bool clone_test(void) {
    BEGIN_TEST;

    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t s = dl_clone_loader_service(&h);
    EXPECT_EQ(s, ZX_OK, "unexpected return value from ioctl");
    EXPECT_NE(h, ZX_HANDLE_INVALID, "invalid handle from ioctl");

    zx_handle_close(h);

    END_TEST;
}

int main(int argc, char** argv);
static bool dladdr_main_test(void) {
    BEGIN_TEST;

    Dl_info info;
    ASSERT_NE(dladdr(&main, &info), 0, "dladdr failed");

    // The "main" symbol is not exported to .dynsym, so it won't be found.
    EXPECT_EQ(info.dli_sname, NULL, "unexpected symbol name");
    EXPECT_EQ(info.dli_saddr, NULL, "unexpected symbol address");

    END_TEST;
}

// TODO(dbort): Test that this process uses the system loader service by default

BEGIN_TEST_CASE(dlfcn_tests)
RUN_TEST(dlopen_vmo_test);
RUN_TEST(loader_service_test);
RUN_TEST(clone_test);
RUN_TEST(dladdr_main_test);
END_TEST_CASE(dlfcn_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
