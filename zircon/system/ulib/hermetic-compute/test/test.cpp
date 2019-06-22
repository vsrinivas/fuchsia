// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-compute.h>

#include <cerrno>
#include <cstring>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <filesystem>
#include <lib/fdio/io.h>
#include <lib/zx/job.h>
#include <string>
#include <unistd.h>
#include <zxtest/zxtest.h>

namespace {

void GetElfVmo(std::filesystem::path module_path, zx::vmo* out_vmo) {
    {
        std::filesystem::path root_dir("/");
        if (const char* var = getenv("TEST_ROOT_DIR")) {
            root_dir = var;
        }
        module_path = root_dir / module_path;
    }

    fbl::unique_fd module_fd(open(module_path.c_str(), O_RDONLY));
    ASSERT_TRUE(module_fd.is_valid(), "cannot open %s: %s",
                module_path.c_str(), strerror(errno));

    ASSERT_OK(fdio_get_vmo_copy(module_fd.get(),
                                out_vmo->reset_and_get_address()));
    ASSERT_OK(out_vmo->replace_as_executable({}, out_vmo));
}

TEST(HermeticComputeTests, BasicModuleTest) {
    constexpr const char kTestModuleFile[] =
        "lib/hermetic/test-module-basic.so";
    zx::vmo module_elf_vmo;
    ASSERT_NO_FATAL_FAILURES(GetElfVmo(kTestModuleFile, &module_elf_vmo),
                             "loading %s", kTestModuleFile);

    HermeticComputeProcess hcp;
    ASSERT_OK(hcp.Init(*zx::job::default_job(), "basic-module-test"));

    // Synchronous load (default vDSO)
    int64_t result;
    ASSERT_OK(hcp.Call(module_elf_vmo, {}, &result, 17, 23));

    EXPECT_EQ(17 + 23, result);
}

TEST(HermeticComputeTests, ManyArgsTest) {
    constexpr const char kTestModuleFile[]
        = "lib/hermetic/test-module-many-args.so";
    zx::vmo module_elf_vmo;
    ASSERT_NO_FATAL_FAILURES(GetElfVmo(kTestModuleFile, &module_elf_vmo),
                             "loading %s", kTestModuleFile);

    HermeticComputeProcess hcp;
    ASSERT_OK(hcp.Init(*zx::job::default_job(), "basic-module-test"));

    // This is enough arguments to require passing some on the stack.
    int64_t result;
    ASSERT_OK(hcp.Call(module_elf_vmo, {}, &result,
                       1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12));

    EXPECT_EQ(1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 11 + 12, result);
}

} // anonymous namespace
