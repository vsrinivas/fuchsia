// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>
#include <link.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <fdio/util.h>
#include <stdio.h>
#include <sys/param.h>
#include <string.h>

#include <unittest/unittest.h>

bool vdso_base_test(void) {
    BEGIN_TEST;

    char msg[128];

    struct link_map* lm = dlopen("libzircon.so", RTLD_NOLOAD);
    snprintf(msg, sizeof(msg), "dlopen(\"libzircon.so\") failed: %s",
             dlerror());
    EXPECT_NONNULL(lm, msg);
    uintptr_t rtld_vdso_base = lm->l_addr;
    int ok = dlclose(lm);
    snprintf(msg, sizeof(msg), "dlclose failed: %s", dlerror());
    EXPECT_EQ(ok, 0, msg);

    uintptr_t prop_vdso_base;
    zx_status_t status =
        zx_object_get_property(zx_process_self(),
                               ZX_PROP_PROCESS_VDSO_BASE_ADDRESS,
                               &prop_vdso_base, sizeof(prop_vdso_base));
    snprintf(msg, sizeof(msg), "zx_object_get_property failed: %d", status);
    EXPECT_EQ(status, 0, msg);

    EXPECT_EQ(rtld_vdso_base, prop_vdso_base,
              "rtld reported address != process property reported address");

    END_TEST;
}

static int phdr_info_callback(struct dl_phdr_info* info, size_t size,
                              void* data) {
    struct dl_phdr_info* key = data;
    if (info->dlpi_addr == key->dlpi_addr) {
        *key = *info;
        return 1;
    }
    return 0;
}

bool vdso_unmap_test(void) {
    BEGIN_TEST;

    char msg[128];

    uintptr_t prop_vdso_base;
    zx_status_t status =
        zx_object_get_property(zx_process_self(),
                               ZX_PROP_PROCESS_VDSO_BASE_ADDRESS,
                               &prop_vdso_base, sizeof(prop_vdso_base));
    snprintf(msg, sizeof(msg), "zx_object_get_property failed: %d", status);
    ASSERT_EQ(status, 0, msg);

    struct dl_phdr_info info = { .dlpi_addr = prop_vdso_base };
    int ret = dl_iterate_phdr(&phdr_info_callback, &info);
    EXPECT_EQ(ret, 1, "dl_iterate_phdr didn't see vDSO?");

    uintptr_t vdso_code_start = 0;
    size_t vdso_code_len = 0;
    for (uint_fast16_t i = 0; i < info.dlpi_phnum; ++i) {
        if (info.dlpi_phdr[i].p_type == PT_LOAD &&
            (info.dlpi_phdr[i].p_flags & PF_X)) {
            vdso_code_start = info.dlpi_addr + info.dlpi_phdr[i].p_vaddr;
            vdso_code_len = info.dlpi_phdr[i].p_memsz;
            break;
        }
    }
    ASSERT_NE(vdso_code_start, 0u, "vDSO has no code segment?");
    ASSERT_NE(vdso_code_len, 0u, "vDSO has no code segment?");

    // Removing the vDSO code mapping is not allowed.
    status = zx_vmar_unmap(zx_vmar_root_self(),
                           vdso_code_start, vdso_code_len);
    EXPECT_EQ(status, ZX_ERR_ACCESS_DENIED, "unmap vDSO code");

    // Nor is removing a whole range overlapping the vDSO code.
    status = zx_vmar_unmap(zx_vmar_root_self(),
                           vdso_code_start - PAGE_SIZE,
                           PAGE_SIZE * 2);
    EXPECT_EQ(status, ZX_ERR_ACCESS_DENIED, "unmap range overlapping vDSO code");

    END_TEST;
}

bool vdso_map_test(void) {
    BEGIN_TEST;

    zx_handle_t vmo = zx_get_startup_handle(PA_HND(PA_VMO_VDSO, 0));
    ASSERT_NE(vmo, ZX_HANDLE_INVALID, "zx_get_startup_handle(PA_HND(PA_VMO_VDSO, 0))");

    // Since we already have a vDSO mapping, loading it again should fail.
    void* h = dlopen_vmo(vmo, RTLD_LOCAL);
    EXPECT_NULL(h, "dlopen_vmo on vDSO VMO succeeded");

    // Create a fresh process that doesn't already have a vDSO mapping.
    // We can't meaningfully test the other constraints on our own
    // process, because the "there can be only one" constraint trumps them.
    const char* name = "vdso_map_test";
    zx_handle_t proc, vmar;
    ASSERT_EQ(zx_process_create(zx_job_default(),
                                name, strlen(name), 0, &proc, &vmar),
              ZX_OK, "zx_process_create failed");

    // This should fail because it's an executable mapping of
    // the wrong portion of the vDSO image (the first page is
    // rodata including the ELF headers).  Only the actual code
    // segment can be mapped executable.
    uintptr_t addr;
    zx_status_t status = zx_vmar_map(
        vmar, 0, vmo, 0, PAGE_SIZE,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_EXECUTE, &addr);
    EXPECT_EQ(status, ZX_ERR_ACCESS_DENIED, "map vDSO data as executable");

    zx_handle_close(proc);
    zx_handle_close(vmar);

    END_TEST;
}

BEGIN_TEST_CASE(vdso_base_tests)
RUN_TEST(vdso_base_test);
RUN_TEST(vdso_unmap_test);
RUN_TEST(vdso_map_test);
END_TEST_CASE(vdso_base_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
