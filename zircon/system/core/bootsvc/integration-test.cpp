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
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>
#include <zircon/boot/bootdata.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "util.h"

namespace {

constexpr char kRootResourcePath[] = "/bootsvc/" fuchsia_boot_RootResource_Name;
constexpr char kArgumentsPath[] = "/bootsvc/" fuchsia_boot_Arguments_Name;

// Make sure that bootsvc passed the bootdata here, and check if it published
// a crashlog if one of the bootdata had one.
bool TestBootdata() {
    BEGIN_TEST;

    fbl::Vector<zx::vmo> bootdata_vmos = bootsvc::RetrieveBootdata();
    ASSERT_GT(bootdata_vmos.size(), 0);

    for (const zx::vmo& vmo : bootdata_vmos) {
        bootdata_t bootdata;
        zx_status_t status = vmo.read(&bootdata, 0, sizeof(bootdata));
        if (status < 0) {
            continue;
        }
        if ((bootdata.type != BOOTDATA_CONTAINER) || (bootdata.extra != BOOTDATA_MAGIC)) {
            continue;
        }
        if (!(bootdata.flags & BOOTDATA_FLAG_V2)) {
            continue;
        }

        size_t len = bootdata.length;
        size_t off = sizeof(bootdata);

        while (len > sizeof(bootdata)) {
            zx_status_t status = vmo.read(&bootdata, off, sizeof(bootdata));
            if (status < 0) {
                break;
            }
            size_t itemlen = BOOTDATA_ALIGN(sizeof(bootdata_t) + bootdata.length);
            if (itemlen > len) {
                break;
            }
            switch (bootdata.type) {
            case BOOTDATA_LAST_CRASHLOG: {
                // If we see a LAST_CRASHLOG entry, then the kernel should have
                // translated it into a VMO file, and bootsvc should have put it
                // at the path below.
                char path[strlen(bootsvc::kLastPanicFilePath) + 7];
                snprintf(path, sizeof(path), "/boot/%s", bootsvc::kLastPanicFilePath);

                auto file_buffer = fbl::make_unique<uint8_t[]>(bootdata.length);
                auto vmo_buffer = fbl::make_unique<uint8_t[]>(bootdata.length);
                fbl::unique_fd fd(open(path, O_RDONLY));
                ASSERT_TRUE(fd.is_valid());
                ASSERT_EQ(read(fd.get(), file_buffer.get(), bootdata.length), bootdata.length);
                ASSERT_EQ(vmo.read(vmo_buffer.get(), off + sizeof(bootdata_t), bootdata.length), ZX_OK);

                ASSERT_BYTES_EQ(file_buffer.get(), vmo_buffer.get(), bootdata.length, "");
                break;
            }
            }
            off += itemlen;
            len -= itemlen;
        }
    }

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

    ASSERT_EQ(ns->count, 2);
    ASSERT_STR_EQ(ns->path[0], "/boot");
    ASSERT_STR_EQ(ns->path[1], "/bootsvc");

    free(ns);
    END_TEST;
}

// Make sure the RootResource service works
bool TestRootResource() {
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

// Make sure that we can parse boot args from a configuration string
bool TestParseBootArgs() {
    BEGIN_TEST;

    const char config1[] = R"(
# comment
key
key=value
=value
)";

    // Parse a valid config.
    fbl::Vector<char> buf;
    zx_status_t status = bootsvc::ParseBootArgs(config1, &buf);
    ASSERT_EQ(ZX_OK, status);

    const char expected[] = "key\0key=value";
    auto actual = reinterpret_cast<const uint8_t*>(buf.get());
    ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected), actual, buf.size(), "");

    // Parse an invalid config.
    const char config2[] = "k ey=value";
    status = bootsvc::ParseBootArgs(config2, &buf);
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);

    END_TEST;
}

// Make sure the Arguments service works
bool TestArguments() {
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
RUN_TEST(TestBootdata)
RUN_TEST(TestLoader)
RUN_TEST(TestNamespace)
RUN_TEST(TestRootResource)
RUN_TEST(TestParseBootArgs)
RUN_TEST(TestArguments)
RUN_TEST(TestVdsosPresent)
END_TEST_CASE(bootsvc_integration_tests)
