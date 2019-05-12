// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "util.h"

static fbl::Vector<fbl::String> arguments;

int main(int argc, char** argv) {
    // Copy arguments for later use in tests.
    for (int i = 0; i < argc; ++i) {
        arguments.push_back(fbl::String(argv[i]));
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}

namespace {

constexpr char kArgumentsPath[] = "/bootsvc/" fuchsia_boot_Arguments_Name;
constexpr char kItemsPath[] = "/bootsvc/" fuchsia_boot_Items_Name;
constexpr char kRootJobPath[] = "/bootsvc/" fuchsia_boot_RootJob_Name;
constexpr char kRootResourcePath[] = "/bootsvc/" fuchsia_boot_RootResource_Name;

// Make sure the loader works
bool TestLoader() {
    BEGIN_TEST;

    // Request loading a library we don't use
    void* ptr = dlopen("libdriver.so", RTLD_LAZY | RTLD_LOCAL);
    ASSERT_NOT_NULL(ptr);
    dlclose(ptr);

    END_TEST;
}

// Make sure that bootsvc gave us a namespace with only /boot and /bootsvc.
bool TestNamespace() {
    BEGIN_TEST;

    fdio_flat_namespace_t* ns;
    ASSERT_EQ(fdio_ns_export_root(&ns), ZX_OK);

    // Close the cloned handles, since we don't need them
    for (size_t i = 0; i < ns->count; ++i) {
        zx_handle_close(ns->handle[i]);
    }

    ASSERT_EQ(ns->count, 2);
    EXPECT_STR_EQ(ns->path[0], "/boot");
    EXPECT_STR_EQ(ns->path[1], "/bootsvc");

    free(ns);
    END_TEST;
}

// Make sure that bootsvc passed along program arguments from bootsvc.next
// correctly.
//
// As documented in TESTING, this test relies on these tests being run by using
// a boot cmdline that includes 'bootsvc.next=bin/bootsvc-tests,testargument' so
// that we can test the parsing on bootsvc.next.
bool TestArguments() {
    BEGIN_TEST;

    ASSERT_EQ(arguments.size(), 2);
    EXPECT_STR_EQ(arguments[0].c_str(), "bin/bootsvc-integration-test");
    EXPECT_STR_EQ(arguments[1].c_str(), "testargument");

    END_TEST;
}

// Make sure the fuchsia.boot.Arguments service works
bool TestBootArguments() {
    BEGIN_TEST;

    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    ASSERT_EQ(ZX_OK, status);

    // Check that we can open the fuchsia.boot.Arguments service.
    status = fdio_service_connect(kArgumentsPath, remote.release());
    ASSERT_EQ(ZX_OK, status);

    // Check that we received a VMO from the service, each time we call it.
    for (size_t i = 0; i < 8; i++) {
        zx::vmo vmo;
        size_t size;
        status = fuchsia_boot_ArgumentsGet(local.get(), vmo.reset_and_get_address(), &size);
        ASSERT_EQ(ZX_OK, status);
        ASSERT_TRUE(vmo.is_valid());

        // Check that the VMO is read-only.
        zx_info_handle_basic_t info;
        status = vmo.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
        ASSERT_EQ(ZX_OK, status);
        ASSERT_EQ(ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHT_WRITE, info.rights);
    }

    END_TEST;
}

// Make sure the fuchsia.boot.Items service works
bool TestBootItems() {
    BEGIN_TEST;

    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    ASSERT_EQ(ZX_OK, status);

    // Check that we can open the fuchsia.boot.Items service.
    status = fdio_service_connect(kItemsPath, remote.release());
    ASSERT_EQ(ZX_OK, status);

    // Check that we can get the following boot item types.
    uint32_t types[] = {
        ZBI_TYPE_CRASHLOG,
        ZBI_TYPE_PLATFORM_ID,
        ZBI_TYPE_STORAGE_RAMDISK,
    };
    for (uint32_t type : types) {
        zx::vmo payload;
        uint32_t length;
        status = fuchsia_boot_ItemsGet(local.get(), type, 0, payload.reset_and_get_address(),
                                       &length);
        ASSERT_EQ(ZX_OK, status);

        // If we see a ZBI_TYPE_CRASHLOG item, then the kernel should have
        // translated it into a VMO file, and bootsvc should have put it at the
        // path below.
        if (type == ZBI_TYPE_CRASHLOG && payload.is_valid()) {
            fbl::String path = fbl::StringPrintf("/boot/%s", bootsvc::kLastPanicFilePath);
            fbl::unique_fd fd(open(path.data(), O_RDONLY));
            ASSERT_TRUE(fd.is_valid());

            auto file_buf = std::make_unique<uint8_t[]>(length);
            auto payload_buf = std::make_unique<uint8_t[]>(length);
            ASSERT_EQ(length, read(fd.get(), file_buf.get(), length));
            ASSERT_EQ(ZX_OK, payload.read(payload_buf.get(), 0, length));
            ASSERT_BYTES_EQ(file_buf.get(), payload_buf.get(), length, "");
        }
    }

    END_TEST;
}

// Make sure the fuchsia.boot.RootJob service works
bool TestBootRootJob() {
    BEGIN_TEST;

    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    ASSERT_EQ(ZX_OK, status);

    // Check that we can open the fuchsia.boot.RootJob service.
    status = fdio_service_connect(kRootJobPath, remote.release());
    ASSERT_EQ(ZX_OK, status);

    // Check that we received a job from the service.
    zx::job root_job;
    status = fuchsia_boot_RootJobGet(local.get(), root_job.reset_and_get_address());
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(root_job.is_valid());

    END_TEST;
}

// Make sure the fuchsia.boot.RootResource service works
bool TestBootRootResource() {
    BEGIN_TEST;

    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    ASSERT_EQ(ZX_OK, status);

    // Check that we can open the fuchsia.boot.RootResource service.
    status = fdio_service_connect(kRootResourcePath, remote.release());
    ASSERT_EQ(ZX_OK, status);

    // Check that we received a resource from the service.
    zx::resource root_resource;
    status = fuchsia_boot_RootResourceGet(local.get(), root_resource.reset_and_get_address());
    ASSERT_EQ(ZX_OK, status);
    ASSERT_TRUE(root_resource.is_valid());

    // Check that a subsequent call results in a peer closed.
    status = fuchsia_boot_RootResourceGet(local.get(), root_resource.reset_and_get_address());
    ASSERT_EQ(ZX_ERR_PEER_CLOSED, status);

    END_TEST;
}

// Check that the kernel-provided VDSOs were added to /boot/kernel/vdso
bool TestVdsosPresent() {
    BEGIN_TEST;

    DIR* dir = opendir("/boot/kernel/vdso");
    ASSERT_NOT_NULL(dir);

    size_t count = 0;
    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (!strcmp(entry->d_name, ".")) {
            continue;
        }
        ASSERT_EQ(entry->d_type, DT_REG);
        ++count;
    }
    ASSERT_GT(count, 0);

    closedir(dir);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(bootsvc_integration_tests)
RUN_TEST(TestLoader)
RUN_TEST(TestNamespace)
RUN_TEST(TestArguments)
RUN_TEST(TestBootArguments)
RUN_TEST(TestBootItems)
RUN_TEST(TestBootRootJob)
RUN_TEST(TestBootRootResource)
RUN_TEST(TestVdsosPresent)
END_TEST_CASE(bootsvc_integration_tests)
