// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <lib/unittest/unittest.h>

#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/interrupt_tracker.h>
#include <vm/pmm.h>
#include <vm/scanner.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>

static constexpr uint kMmuFlags =
    ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE;

static bool hypervisor_supported() {
#if ARCH_ARM64
  if (arm64_get_boot_el() < 2) {
    unittest_printf("Hypervisor not supported\n");
    return false;
  }
#endif
  return true;
}

static zx_status_t get_paddr(void* context, size_t offset, size_t index, paddr_t pa) {
  *static_cast<paddr_t*>(context) = pa;
  return ZX_OK;
}

static zx_status_t create_vmo(size_t vmo_size, fbl::RefPtr<VmObjectPaged>* vmo) {
  return VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, vmo_size, vmo);
}

static zx_status_t commit_vmo(fbl::RefPtr<VmObjectPaged> vmo) {
  zx_status_t status = vmo->CommitRange(0, vmo->size());
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

static zx_status_t create_gpas(ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace>* gpas) {
#if ARCH_ARM64
  return hypervisor::GuestPhysicalAddressSpace::Create(1 /* vmid */, gpas);
#elif ARCH_X86
  return hypervisor::GuestPhysicalAddressSpace::Create(gpas);
#endif
}

static zx_status_t create_mapping(fbl::RefPtr<VmAddressRegion> vmar, fbl::RefPtr<VmObjectPaged> vmo,
                                  zx_gpaddr_t addr, uint mmu_flags = kMmuFlags) {
  fbl::RefPtr<VmMapping> mapping;
  return vmar->CreateVmMapping(addr, vmo->size(), 0 /* align_pow2 */, VMAR_FLAG_SPECIFIC, vmo,
                               0 /* vmo_offset */, mmu_flags, "vmo", &mapping);
}

static zx_status_t create_sub_vmar(fbl::RefPtr<VmAddressRegion> vmar, size_t offset, size_t size,
                                   fbl::RefPtr<VmAddressRegion>* sub_vmar) {
  return vmar->CreateSubVmar(offset, size, 0 /* align_pow2 */, vmar->flags() | VMAR_FLAG_SPECIFIC,
                             "vmar", sub_vmar);
}

static bool guest_physical_address_space_unmap_range() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  zx_status_t status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  fbl::RefPtr<VmObjectPaged> vmo;
  status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Unmap page.
  status = gpas->UnmapRange(0, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "Failed to unmap page from GuestPhysicalAddressSpace\n");

  // Verify GetPage for unmapped address fails.
  zx_paddr_t gpas_paddr;
  status = gpas->GetPage(0, &gpas_paddr);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "GetPage returning unexpected value for unmapped address\n");

  END_TEST;
}

static bool guest_physical_address_space_unmap_range_outside_of_mapping() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  zx_status_t status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  fbl::RefPtr<VmObjectPaged> vmo;
  status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Unmap page.
  status = gpas->UnmapRange(PAGE_SIZE * 8, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "Failed to unmap page from GuestPhysicalAddressSpace\n");

  END_TEST;
}

static bool guest_physical_address_space_unmap_range_multiple_mappings() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  zx_status_t status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");

  fbl::RefPtr<VmObjectPaged> vmo1;
  status = create_vmo(PAGE_SIZE * 2, &vmo1);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(gpas->RootVmar(), vmo1, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  fbl::RefPtr<VmObjectPaged> vmo2;
  status = create_vmo(PAGE_SIZE * 2, &vmo2);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(gpas->RootVmar(), vmo2, PAGE_SIZE * 3);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Unmap pages.
  status = gpas->UnmapRange(PAGE_SIZE, PAGE_SIZE * 3);
  EXPECT_EQ(ZX_OK, status, "Failed to multiple unmap pages from GuestPhysicalAddressSpace\n");

  // Verify GetPage for unmapped addresses fails.
  zx_paddr_t gpas_paddr = 0;
  for (zx_gpaddr_t addr = PAGE_SIZE; addr < PAGE_SIZE * 4; addr += PAGE_SIZE) {
    status = gpas->GetPage(addr, &gpas_paddr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status,
              "GetPage returning unexpected value for unmapped address\n");
  }

  // Verify GetPage for mapped addresses succeeds.
  status = gpas->GetPage(0, &gpas_paddr);
  EXPECT_EQ(ZX_OK, status, "Failed to read page from GuestPhysicalAddressSpace\n");
  status = gpas->GetPage(PAGE_SIZE * 4, &gpas_paddr);
  EXPECT_EQ(ZX_OK, status, "Failed to read page from GuestPhysicalAddressSpace\n");

  END_TEST;
}

static bool guest_physical_address_space_unmap_range_sub_region() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  zx_status_t status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  fbl::RefPtr<VmAddressRegion> root_vmar = gpas->RootVmar();
  // To test partial unmapping within sub-VMAR:
  // Sub-VMAR from [0, PAGE_SIZE * 2).
  // Map within sub-VMAR from [PAGE_SIZE, PAGE_SIZE * 2).
  fbl::RefPtr<VmAddressRegion> sub_vmar1;
  status = create_sub_vmar(root_vmar, 0, PAGE_SIZE * 2, &sub_vmar1);
  EXPECT_EQ(ZX_OK, status, "Failed to create sub-VMAR\n");
  EXPECT_TRUE(sub_vmar1->has_parent(), "Sub-VMAR does not have a parent");
  fbl::RefPtr<VmObjectPaged> vmo1;
  status = create_vmo(PAGE_SIZE, &vmo1);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(sub_vmar1, vmo1, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");
  // To test destroying of sub-VMAR:
  // Sub-VMAR from [PAGE_SIZE * 2, PAGE_SIZE * 3).
  // Map within sub-VMAR from [0, PAGE_SIZE).
  fbl::RefPtr<VmAddressRegion> sub_vmar2;
  status = create_sub_vmar(root_vmar, PAGE_SIZE * 2, PAGE_SIZE, &sub_vmar2);
  EXPECT_EQ(ZX_OK, status, "Failed to create sub-VMAR\n");
  EXPECT_TRUE(sub_vmar2->has_parent(), "Sub-VMAR does not have a parent");
  fbl::RefPtr<VmObjectPaged> vmo2;
  status = create_vmo(PAGE_SIZE, &vmo2);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(sub_vmar2, vmo2, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");
  // To test partial unmapping within root-VMAR:
  // Map within root-VMAR from [PAGE_SIZE * 3, PAGE_SIZE * 5).
  fbl::RefPtr<VmObjectPaged> vmo3;
  status = create_vmo(PAGE_SIZE * 2, &vmo3);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(root_vmar, vmo3, PAGE_SIZE * 3);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Unmap pages from [PAGE_SIZE, PAGE_SIZE * 4).
  status = gpas->UnmapRange(PAGE_SIZE, PAGE_SIZE * 3);
  EXPECT_EQ(ZX_OK, status, "Failed to multiple unmap pages from GuestPhysicalAddressSpace\n");

  // Verify GetPage for unmapped addresses fails.
  zx_paddr_t gpas_paddr = 0;
  for (zx_gpaddr_t addr = 0; addr < PAGE_SIZE * 4; addr += PAGE_SIZE) {
    status = gpas->GetPage(addr, &gpas_paddr);
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status,
              "GetPage returning unexpected value for unmapped address\n");
  }

  // Verify GetPage for mapped addresses succeeds.
  status = gpas->GetPage(PAGE_SIZE * 4, &gpas_paddr);
  EXPECT_EQ(ZX_OK, status, "Failed to read page from GuestPhysicalAddressSpace\n");

  // Verify that sub-VMARs still have a parent.
  EXPECT_TRUE(sub_vmar1->has_parent(), "Sub-VMAR does not have a parent");
  EXPECT_TRUE(sub_vmar2->has_parent(), "Sub-VMAR does not have a parent");

  END_TEST;
}

static bool guest_physical_address_space_get_page() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  AutoVmScannerDisable scanner_disable;

  // Setup.
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  zx_status_t status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  fbl::RefPtr<VmObjectPaged> vmo;
  status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Commit VMO.
  status = commit_vmo(vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to commit VMO\n");

  // Read expected physical address from the VMO.
  zx_paddr_t vmo_paddr = 0;
  status = vmo->Lookup(0, PAGE_SIZE, get_paddr, &vmo_paddr);
  EXPECT_EQ(ZX_OK, status, "Failed to lookup physical address of VMO\n");
  EXPECT_NE(0u, vmo_paddr, "Failed to lookup physical address of VMO\n");

  // Read physical address from GPAS & compare with address read from VMO.
  zx_paddr_t gpas_paddr = 0;
  status = gpas->GetPage(0, &gpas_paddr);
  EXPECT_EQ(ZX_OK, status, "Failed to read page from GuestPhysicalAddressSpace\n");
  EXPECT_EQ(vmo_paddr, gpas_paddr,
            "Incorrect physical address returned from GuestPhysicalAddressSpace::GetPage\n");

  END_TEST;
}

static bool guest_physical_address_space_get_page_complex() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  AutoVmScannerDisable scanner_disable;

  // Test GetPage with a less trivial VMAR configuration.
  //
  //                  0 -->+--------+
  //                       |  Root  |
  //                       |  VMO   |
  //      ROOT_VMO_SIZE -->---------+ +--------+
  //                       |        | | Second |
  // ROOT_VMO_SIZE +       |        | | VMO    |
  //    SECOND_VMO_SIZE -->---------+ +--------+
  //                       |  Root  | | Shadow |
  //                       |  VMAR  | | VMAR   |
  //                        ~~~~~~~~   ~~~~~~~~
  //
  // The 'Root VMO/VMAR' is the default configuration when initializing
  // GuestPhysicalAddressSpace with a VMO size of 'PAGE_SIZE'. This test
  // allocates a second VMAR and VMO and attaches them both into the 'Root
  // VMAR' to ensure we correctly locate addresses in these structures.
  const uint ROOT_VMO_SIZE = PAGE_SIZE;
  const uint SECOND_VMO_SIZE = PAGE_SIZE;

  // Setup.
  fbl::RefPtr<VmObjectPaged> vmo1;
  zx_status_t status = create_vmo(ROOT_VMO_SIZE, &vmo1);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  fbl::RefPtr<VmAddressRegion> root_vmar = gpas->RootVmar();
  status = create_mapping(root_vmar, vmo1, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Commit first VMO.
  status = commit_vmo(vmo1);
  EXPECT_EQ(ZX_OK, status, "Failed to commit VMO\n");

  // Allocate second VMAR, offset one page into the root.
  fbl::RefPtr<VmAddressRegion> shadow_vmar;
  status =
      create_sub_vmar(root_vmar, ROOT_VMO_SIZE, root_vmar->size() - ROOT_VMO_SIZE, &shadow_vmar);
  EXPECT_EQ(ZX_OK, status, "Failed to create shadow VMAR\n");

  // Allocate second VMO; we'll map the original VMO on top of this one.
  fbl::RefPtr<VmObjectPaged> vmo2;
  status = create_vmo(SECOND_VMO_SIZE, &vmo2);
  EXPECT_EQ(ZX_OK, status, "Failed allocate second VMO\n");

  // Commit second VMO.
  status = commit_vmo(vmo2);
  EXPECT_EQ(ZX_OK, status, "Failed to commit second VMO\n");

  // Map second VMO into second VMAR.
  status = create_mapping(shadow_vmar, vmo2, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to map vmo into shadow vmar\n");

  // Read expected physical address from the VMO.
  zx_paddr_t vmo_paddr = 0;
  status = vmo2->Lookup(0, PAGE_SIZE, get_paddr, &vmo_paddr);
  EXPECT_EQ(ZX_OK, status, "Failed to lookup physical address of VMO\n");
  EXPECT_NE(0u, vmo_paddr, "Failed to lookup physical address of VMO\n");

  // Read physical address from GPAS.
  zx_paddr_t gpas_paddr = 0;
  status = gpas->GetPage(ROOT_VMO_SIZE, &gpas_paddr);
  EXPECT_EQ(ZX_OK, status, "Failed to read page from GuestPhysicalAddressSpace\n");
  EXPECT_EQ(vmo_paddr, gpas_paddr,
            "Incorrect physical address returned from GuestPhysicalAddressSpace::GetPage\n");
  END_TEST;
}

static bool guest_physical_address_space_get_page_not_present() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  AutoVmScannerDisable scanner_disable;

  // Setup.
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  zx_status_t status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  fbl::RefPtr<VmObjectPaged> vmo;
  status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Commit VMO.
  status = commit_vmo(vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to commit VMO\n");

  // Query unmapped address.
  zx_paddr_t gpas_paddr = 0;
  status = gpas->GetPage(UINTPTR_MAX, &gpas_paddr);
  EXPECT_EQ(ZX_ERR_NOT_FOUND, status, "GetPage returning unexpected value for unmapped address\n");

  END_TEST;
}

static bool guest_physical_address_space_page_fault() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  zx_status_t status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  fbl::RefPtr<VmObjectPaged> vmo;
  status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");
  status = create_mapping(gpas->RootVmar(), vmo, PAGE_SIZE, ARCH_MMU_FLAG_PERM_READ);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");
  status = create_mapping(gpas->RootVmar(), vmo, PAGE_SIZE * 2,
                          ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");
  status = create_mapping(gpas->RootVmar(), vmo, PAGE_SIZE * 3,
                          ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_EXECUTE);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Fault in each page.
  for (zx_gpaddr_t addr = 0; addr < PAGE_SIZE * 4; addr += PAGE_SIZE) {
    status = gpas->PageFault(addr);
    EXPECT_EQ(ZX_OK, status, "Failed to fault page\n");
  }

  END_TEST;
}

static bool guest_physical_address_space_map_interrupt_controller() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  zx_status_t status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  fbl::RefPtr<VmObjectPaged> vmo;
  status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  // Allocate a page to use as the interrupt controller.
  paddr_t paddr = 0;
  vm_page* vm_page;
  status = pmm_alloc_page(0, &vm_page, &paddr);
  EXPECT_EQ(ZX_OK, status, "Unable to allocate a page\n");

  // Map interrupt controller page in an arbitrary location.
  const vaddr_t APIC_ADDRESS = 0xffff0000;
  status = gpas->MapInterruptController(APIC_ADDRESS, paddr, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "Failed to map APIC page\n");

  // Cleanup
  pmm_free_page(vm_page);
  END_TEST;
}

static bool guest_physical_address_space_uncached() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = vmo->SetMappingCachePolicy(ZX_CACHE_POLICY_UNCACHED);
  EXPECT_EQ(ZX_OK, status, "Failed to set cache policy\n");

  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  END_TEST;
}

static bool guest_physical_address_space_uncached_device() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = vmo->SetMappingCachePolicy(ZX_CACHE_POLICY_UNCACHED_DEVICE);
  EXPECT_EQ(ZX_OK, status, "Failed to set cache policy\n");

  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  END_TEST;
}

static bool guest_physical_address_space_write_combining() {
  BEGIN_TEST;

  if (!hypervisor_supported()) {
    return true;
  }

  // Setup.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = create_vmo(PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "Failed to create VMO\n");
  status = vmo->SetMappingCachePolicy(ZX_CACHE_POLICY_WRITE_COMBINING);
  EXPECT_EQ(ZX_OK, status, "Failed to set cache policy\n");

  ktl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas;
  status = create_gpas(&gpas);
  EXPECT_EQ(ZX_OK, status, "Failed to create GuestPhysicalAddressSpace\n");
  status = create_mapping(gpas->RootVmar(), vmo, 0);
  EXPECT_EQ(ZX_OK, status, "Failed to create mapping\n");

  END_TEST;
}

static bool interrupt_bitmap() {
  BEGIN_TEST;

  hypervisor::InterruptBitmap<8> bitmap;

  uint32_t vector = UINT32_MAX;
  ASSERT_EQ(ZX_OK, bitmap.Init());
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(0));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(1));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Scan(&vector));
  EXPECT_EQ(UINT32_MAX, vector);

  // Index 0.
  vector = UINT32_MAX;
  bitmap.Set(0u, hypervisor::InterruptType::VIRTUAL);
  EXPECT_EQ(hypervisor::InterruptType::VIRTUAL, bitmap.Get(0));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(1));
  EXPECT_EQ(hypervisor::InterruptType::VIRTUAL, bitmap.Scan(&vector));
  EXPECT_EQ(0u, vector);

  vector = UINT32_MAX;
  bitmap.Set(0u, hypervisor::InterruptType::PHYSICAL);
  EXPECT_EQ(hypervisor::InterruptType::PHYSICAL, bitmap.Get(0u));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(1u));
  EXPECT_EQ(hypervisor::InterruptType::PHYSICAL, bitmap.Scan(&vector));
  EXPECT_EQ(0u, vector);

  vector = UINT32_MAX;
  bitmap.Set(0u, hypervisor::InterruptType::INACTIVE);
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(0u));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(1u));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Scan(&vector));
  EXPECT_EQ(UINT32_MAX, vector);

  // Index 1.
  vector = UINT32_MAX;
  bitmap.Set(1u, hypervisor::InterruptType::VIRTUAL);
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(0u));
  EXPECT_EQ(hypervisor::InterruptType::VIRTUAL, bitmap.Get(1u));
  EXPECT_EQ(hypervisor::InterruptType::VIRTUAL, bitmap.Scan(&vector));
  EXPECT_EQ(1u, vector);

  vector = UINT32_MAX;
  bitmap.Set(1u, hypervisor::InterruptType::PHYSICAL);
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(0u));
  EXPECT_EQ(hypervisor::InterruptType::PHYSICAL, bitmap.Get(1u));
  EXPECT_EQ(hypervisor::InterruptType::PHYSICAL, bitmap.Scan(&vector));
  EXPECT_EQ(1u, vector);

  vector = UINT32_MAX;
  bitmap.Set(1u, hypervisor::InterruptType::INACTIVE);
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(0u));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(1u));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Scan(&vector));
  EXPECT_EQ(UINT32_MAX, vector);

  // Clear
  bitmap.Set(0u, hypervisor::InterruptType::VIRTUAL);
  bitmap.Set(1u, hypervisor::InterruptType::VIRTUAL);
  bitmap.Set(2u, hypervisor::InterruptType::PHYSICAL);
  bitmap.Set(3u, hypervisor::InterruptType::PHYSICAL);
  bitmap.Clear(1u, 3u);
  EXPECT_EQ(hypervisor::InterruptType::VIRTUAL, bitmap.Get(0u));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(1u));
  EXPECT_EQ(hypervisor::InterruptType::INACTIVE, bitmap.Get(2u));
  EXPECT_EQ(hypervisor::InterruptType::PHYSICAL, bitmap.Get(3u));

  END_TEST;
}

// Use the function name as the test name
#define HYPERVISOR_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(hypervisor)
HYPERVISOR_UNITTEST(guest_physical_address_space_unmap_range)
HYPERVISOR_UNITTEST(guest_physical_address_space_unmap_range_outside_of_mapping)
HYPERVISOR_UNITTEST(guest_physical_address_space_unmap_range_multiple_mappings)
HYPERVISOR_UNITTEST(guest_physical_address_space_unmap_range_sub_region)
HYPERVISOR_UNITTEST(guest_physical_address_space_get_page)
HYPERVISOR_UNITTEST(guest_physical_address_space_get_page_complex)
HYPERVISOR_UNITTEST(guest_physical_address_space_get_page_not_present)
HYPERVISOR_UNITTEST(guest_physical_address_space_page_fault)
HYPERVISOR_UNITTEST(guest_physical_address_space_map_interrupt_controller)
HYPERVISOR_UNITTEST(guest_physical_address_space_uncached)
HYPERVISOR_UNITTEST(guest_physical_address_space_uncached_device)
HYPERVISOR_UNITTEST(guest_physical_address_space_write_combining)
HYPERVISOR_UNITTEST(interrupt_bitmap)
UNITTEST_END_TESTCASE(hypervisor, "hypervisor", "Hypervisor unit tests.")
