// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/user_memory.h>

#include <fbl/auto_call.h>
#include <lib/unittest/unittest.h>

namespace testing {

UserMemory::~UserMemory() {
    zx_status_t status = mapping_->Unmap(mapping_->base(), mapping_->size());
    DEBUG_ASSERT(status == ZX_OK);
}

// static
fbl::unique_ptr<UserMemory> UserMemory::Create(size_t size) {
    size = ROUNDUP_PAGE_SIZE(size);

    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, size, &vmo);
    if (status != ZX_OK) {
        unittest_printf("VmObjectPaged::Create failed: %d\n", status);
        return nullptr;
    }

    fbl::RefPtr<VmAspace> aspace = fbl::WrapRefPtr(vmm_aspace_to_obj(get_current_thread()->aspace));
    DEBUG_ASSERT(aspace);
    DEBUG_ASSERT(aspace->is_user());
    fbl::RefPtr<VmAddressRegion> root_vmar = aspace->RootVmar();
    constexpr uint32_t vmar_flags = VMAR_FLAG_CAN_MAP_READ |
                                    VMAR_FLAG_CAN_MAP_WRITE |
                                    VMAR_FLAG_CAN_MAP_EXECUTE;
    fbl::RefPtr<VmMapping> mapping;
    constexpr uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_READ |
                                    ARCH_MMU_FLAG_PERM_WRITE;
    status = root_vmar->CreateVmMapping(/* offset= */ 0, size, /* align_pow2= */ 0, vmar_flags,
                                        vmo, 0, arch_mmu_flags, "unittest", &mapping);
    if (status != ZX_OK) {
        unittest_printf("CreateVmMapping failed: %d\n", status);
        return nullptr;
    }
    auto unmap = fbl::MakeAutoCall([&]() {
        if (mapping) {
            zx_status_t status = mapping->Unmap(mapping->base(), mapping->size());
            DEBUG_ASSERT(status == ZX_OK);
        }
    });

    fbl::AllocChecker ac;
    fbl::unique_ptr<UserMemory> mem(new (&ac) UserMemory(mapping));
    if (!ac.check()) {
        unittest_printf("failed to allocate from heap\n");
        return nullptr;
    }
    // Unmapping is now UserMemory's responsibility.
    unmap.cancel();

    return mem;
}

} // namespace testing
