// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/job.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace {

// Check that bootsvc put the boot cmdline in env
bool TestBootCmdline() {
    BEGIN_TEST;
    ASSERT_STR_EQ(getenv("bootsvc.next"), "bin/bootsvc-tests");
    END_TEST;
}

// Make sure the loader works
bool TestLoader() {
    BEGIN_TEST;

    // Request loading a library we don't use
    void* ptr = dlopen("libdriver.so", RTLD_LAZY | RTLD_LOCAL);
    ASSERT_NOT_NULL(ptr);
    dlclose(ptr);

    END_TEST;
}

// Make sure that bootsvc gave us a namespace with only /boot
bool TestNamespace() {
    BEGIN_TEST;

    fdio_flat_namespace_t* ns;
    ASSERT_EQ(fdio_ns_export_root(&ns), ZX_OK);

    // Close the cloned handles, since we don't need them
    for (size_t i = 0; i < ns->count; ++i) {
        zx_handle_close(ns->handle[i]);
    }

    ASSERT_EQ(ns->count, 1);
    ASSERT_STR_EQ(ns->path[0], "/boot");

    free(ns);
    END_TEST;
}

// Check that bootsvc gave us the expected handles
bool TestStartupHandles() {
    BEGIN_TEST;

    // Check we were given a resource handle (should be the root one)
    zx::handle root_resource(zx_take_startup_handle(PA_HND(PA_RESOURCE, 0)));
    ASSERT_TRUE(root_resource.is_valid());
    zx_info_handle_basic_t basic;
    ASSERT_EQ(root_resource.get_info(ZX_INFO_HANDLE_BASIC, &basic, sizeof(basic), nullptr, nullptr),
              ZX_OK);
    ASSERT_EQ(basic.type, ZX_OBJ_TYPE_RESOURCE);

    // Check we were given a job handle (should be the root job)
    ASSERT_TRUE(zx::job::default_job()->is_valid());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(bootsvc_integration_tests)
RUN_TEST(TestBootCmdline)
RUN_TEST(TestLoader)
RUN_TEST(TestNamespace)
RUN_TEST(TestStartupHandles)
END_TEST_CASE(bootsvc_integration_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}

