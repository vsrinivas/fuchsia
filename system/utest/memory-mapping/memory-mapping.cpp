// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <unistd.h>

#include <magenta/syscalls.h>
#include <mxu/unittest.h>

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
    mx_handle_t vmo = _magenta_vm_object_create(page_size);
    EXPECT_LT(0, vmo, "vm_object_create");

    // This is the lowest non-canonical address on x86-64.  We want to
    // make sure that userland cannot map a page immediately below
    // this address.  See docs/magenta/sysret_problem.md for an
    // explanation of the reason.
    uintptr_t noncanon_addr =
        ((uintptr_t) 1) << (x86_linear_address_width() - 1);

    // Check that we cannot map a page ending at |noncanon_addr|.
    uintptr_t addr = noncanon_addr - page_size;
    mx_status_t status = _magenta_process_vm_map(
        0, vmo, 0, page_size, &addr,
        MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_FIXED);
    EXPECT_EQ(ERR_NO_MEMORY, status, "vm_map");

    // Check that we can map at the next address down.  This helps to
    // verify that the previous check didn't fail for some unexpected
    // reason.
    addr = noncanon_addr - page_size * 2;
    status = _magenta_process_vm_map(
        0, vmo, 0, page_size, &addr,
        MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_FIXED);
    EXPECT_EQ(NO_ERROR, status, "vm_map");

    // Check that MX_VM_FLAG_FIXED fails on already-mapped locations.
    // Otherwise, the previous mapping could have overwritten
    // something that was in use, which could cause problems later.
    status = _magenta_process_vm_map(
        0, vmo, 0, page_size, &addr,
        MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_FIXED);
    EXPECT_EQ(ERR_NO_MEMORY, status, "vm_map");

    // Clean up.
    status = _magenta_process_vm_unmap(0, addr, 0);
    EXPECT_EQ(NO_ERROR, status, "vm_unmap");
    status = _magenta_handle_close(vmo);
    EXPECT_EQ(NO_ERROR, status, "handle_close");
#endif

    END_TEST;
}

}

BEGIN_TEST_CASE(memory_mapping_tests)
RUN_TEST(address_space_limits_test);
END_TEST_CASE(memory_mapping_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests();
    return success ? 0 : -1;
}
