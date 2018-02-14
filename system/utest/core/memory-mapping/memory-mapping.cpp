// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <unistd.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <unittest/unittest.h>
#include <sys/mman.h>

namespace {

#if defined(__x86_64__)

// This is based on code from kernel/ which isn't usable by code in system/.
enum { X86_CPUID_ADDR_WIDTH = 0x80000008 };

uint32_t x86_linear_address_width() {
    uint32_t eax, ebx, ecx, edx;
    __asm__("cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(X86_CPUID_ADDR_WIDTH), "c"(0));
    return (eax >> 8) & 0xff;
}

#endif

bool address_space_limits_test() {
    BEGIN_TEST;

#if defined(__x86_64__)
    size_t page_size = getpagesize();
    zx_handle_t vmo;
    EXPECT_EQ(zx_vmo_create(page_size, 0, &vmo), ZX_OK);
    EXPECT_NE(vmo, ZX_HANDLE_INVALID, "vm_object_create");

    // This is the lowest non-canonical address on x86-64.  We want to
    // make sure that userland cannot map a page immediately below
    // this address.  See docs/sysret_problem.md for an explanation of
    // the reason.
    uintptr_t noncanon_addr =
        ((uintptr_t) 1) << (x86_linear_address_width() - 1);

    zx_info_vmar_t vmar_info;
    zx_status_t status = zx_object_get_info(zx_vmar_root_self(), ZX_INFO_VMAR,
                                            &vmar_info, sizeof(vmar_info),
                                            NULL, NULL);
    EXPECT_EQ(ZX_OK, status, "get_info");

    // Check that we cannot map a page ending at |noncanon_addr|.
    size_t offset = noncanon_addr - page_size - vmar_info.base;
    uintptr_t addr;
    status = zx_vmar_map(
        zx_vmar_root_self(), offset, vmo, 0, page_size,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC,
        &addr);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "vm_map");

    // Check that we can map at the next address down.  This helps to
    // verify that the previous check didn't fail for some unexpected
    // reason.
    offset = noncanon_addr - page_size * 2 - vmar_info.base;
    status = zx_vmar_map(
        zx_vmar_root_self(), offset, vmo, 0, page_size,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC,
        &addr);
    EXPECT_EQ(ZX_OK, status, "vm_map");

    // Check that ZX_VM_FLAG_SPECIFIC fails on already-mapped locations.
    // Otherwise, the previous mapping could have overwritten
    // something that was in use, which could cause problems later.
    status = zx_vmar_map(
        zx_vmar_root_self(), offset, vmo, 0, page_size,
        ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_SPECIFIC,
        &addr);
    EXPECT_EQ(ZX_ERR_NO_MEMORY, status, "vm_map");

    // Clean up.
    status = zx_vmar_unmap(zx_vmar_root_self(), addr, page_size);
    EXPECT_EQ(ZX_OK, status, "vm_unmap");
    status = zx_handle_close(vmo);
    EXPECT_EQ(ZX_OK, status, "handle_close");
#endif

    END_TEST;
}

bool mmap_zerofilled_test() {
    BEGIN_TEST;

    char* addr = (char *)mmap(NULL, 16384, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    for (size_t i = 0; i < 16384; i++) {
        EXPECT_EQ('\0', addr[i], "non-zero memory found");
    }
    int unmap_result = munmap(addr, 16384);
    EXPECT_EQ(0, unmap_result, "munmap should have succeeded");

    END_TEST;
}

bool mmap_len_test() {
    BEGIN_TEST;

    uint32_t* addr = (uint32_t*)mmap(NULL, 0, PROT_READ, MAP_PRIVATE|MAP_ANON, -1, 0);
    auto test_errno = errno;
    EXPECT_EQ(MAP_FAILED, addr, "mmap should fail when len == 0");
    EXPECT_EQ(EINVAL, test_errno, "mmap errno should be EINVAL when len == 0");

    addr = (uint32_t*)mmap(NULL, PTRDIFF_MAX, PROT_READ, MAP_PRIVATE|MAP_ANON, -1, 0);
    test_errno = errno;
    EXPECT_EQ(MAP_FAILED, addr, "mmap should fail when len >= PTRDIFF_MAX");
    EXPECT_EQ(ENOMEM, test_errno, "mmap errno should be ENOMEM when len >= PTRDIFF_MAX");

    END_TEST;
}

bool mmap_offset_test() {
    BEGIN_TEST;

    uint32_t* addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_PRIVATE|MAP_ANON, -1, 4);
    auto test_errno = errno;
    EXPECT_EQ(MAP_FAILED, addr, "mmap should fail for unaligned offset");
    EXPECT_EQ(EINVAL, test_errno, "mmap errno should be EINVAL for unaligned offset");

    END_TEST;
}

static __attribute__ ((noinline)) int add(int a, int b) {
    return a+b;
}

bool mmap_PROT_EXEC_test() {
    BEGIN_TEST;

    // allocate 2 pages worth of memory which we will eventually execute
    int PAGE_SZ = getpagesize();
    void *addr = mmap(NULL, PAGE_SZ * 2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    EXPECT_NE(MAP_FAILED, addr, "mmap should have succeeded for PROT_READ|PROT_WRITE");

    // Copy over code from our address space into the newly allocated memory.
    // We assume our add function will never cover more than 2 pages of memory
    // and that the two pages will be readable in memory.
    uintptr_t fp = (uintptr_t) &add;
    uintptr_t page_start = fp & ~(PAGE_SZ - 1);
    memcpy(addr, (void *) page_start, PAGE_SZ * 2);

    // mark the code executable
    int result = mprotect(addr, PAGE_SZ * 2, PROT_READ|PROT_EXEC);
    EXPECT_EQ(0, result, "Unable to mark pages PROT_READ|PROT_EXEC");

    // Execute the code from our new location
    uintptr_t offset = fp - page_start;
    int (*add_func)(int, int) = (int (*)(int, int))((uintptr_t) addr + offset);
    int add_result = add_func(1, 2);

    // Check that the result of adding 1+2 is 3.
    EXPECT_EQ(3, add_result);

    // Deallocate pages
    result = munmap(addr, PAGE_SZ * 2);
    EXPECT_EQ(0, result, "munmap unexpectedly failed");
    END_TEST;
}

bool mmap_prot_test() {
    BEGIN_TEST;

    volatile uint32_t* addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
    EXPECT_NE(MAP_FAILED, addr, "mmap should have succeeded for PROT_NONE");

    addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_PRIVATE|MAP_ANON, -1, 0);
    EXPECT_NE(MAP_FAILED, addr, "mmap failed for read-only alloc");

    // This is somewhat pointless, to have a private read-only mapping, but we
    // should be able to read it.
    EXPECT_EQ(*addr, *addr, "could not read from mmaped address");

    addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    EXPECT_NE(MAP_FAILED, addr, "mmap failed for read-write alloc");

    // Now we test writing to the mapped memory, and verify that we can read it
    // back.
    *addr = 5678u;
    EXPECT_EQ(5678u, *addr, "writing to address returned by mmap failed");

    END_TEST;
}

bool mmap_flags_test() {
    BEGIN_TEST;

    uint32_t* addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_ANON, -1, 0);
    auto test_errno = errno;
    EXPECT_EQ(MAP_FAILED, addr, "mmap should fail without MAP_PRIVATE or MAP_SHARED");
    EXPECT_EQ(EINVAL, test_errno, "mmap errno should be EINVAL with bad flags");

    addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_PRIVATE|MAP_SHARED|MAP_ANON, -1, 0);
    test_errno = errno;
    EXPECT_EQ(MAP_FAILED, addr, "mmap should fail with both MAP_PRIVATE and MAP_SHARED");
    EXPECT_EQ(EINVAL, test_errno, "mmap errno should be EINVAL with bad flags");

    addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_PRIVATE|MAP_ANON, -1, 0);
    EXPECT_NE(MAP_FAILED, addr, "mmap failed with MAP_PRIVATE flags");

    addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_READ, MAP_SHARED|MAP_ANON, -1, 0);
    EXPECT_NE(MAP_FAILED, addr, "mmap failed with MAP_SHARED flags");

    END_TEST;
}

bool mprotect_test() {
    BEGIN_TEST;

    uint32_t* addr = (uint32_t*)mmap(NULL, sizeof(uint32_t), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    ASSERT_NE(MAP_FAILED, addr, "mmap failed to map");

    int page_size = getpagesize();
    // Should be able to write.
    *addr = 10;
    EXPECT_EQ(10u, *addr, "read after write failed");

    int status = mprotect(addr, page_size, PROT_READ);
    EXPECT_EQ(0, status, "mprotect failed to downgrade to read-only");

    ASSERT_DEATH([](void* crashaddr) {
        uint32_t *intptr = static_cast<uint32_t *>(crashaddr);
        *intptr = 12;
    }, addr, "write to addr should have caused a crash");

    status = mprotect(addr, page_size, PROT_WRITE);
    auto test_errno = errno;
    EXPECT_EQ(-1, status, "mprotect should fail for write-only");
    EXPECT_EQ(ENOTSUP, test_errno, "mprotect should return ENOTSUP for write-only");

    status = mprotect(addr, page_size, PROT_NONE);
    test_errno = errno;
    EXPECT_EQ(0, status, "mprotect should succeed for PROT_NONE");

    END_TEST;
}

}

BEGIN_TEST_CASE(memory_mapping_tests)
RUN_TEST(address_space_limits_test);
RUN_TEST(mmap_zerofilled_test);
RUN_TEST(mmap_len_test);
RUN_TEST(mmap_PROT_EXEC_test);
RUN_TEST(mmap_offset_test);
RUN_TEST(mmap_prot_test);
RUN_TEST(mmap_flags_test);
RUN_TEST_ENABLE_CRASH_HANDLER(mprotect_test);
END_TEST_CASE(memory_mapping_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif
