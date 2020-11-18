// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "test_helper.h"

namespace vm_unittest {

// Creates a vm object.
static bool vmo_create_test() {
  BEGIN_TEST;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE, &vmo);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_TRUE(vmo);
  EXPECT_FALSE(vmo->is_contiguous(), "vmo is not contig\n");
  EXPECT_FALSE(vmo->is_resizable(), "vmo is not resizable\n");
  END_TEST;
}

static bool vmo_create_maximum_size() {
  BEGIN_TEST;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, 0xfffffffffffe0000, &vmo);
  EXPECT_EQ(status, ZX_OK, "should be ok\n");

  status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, 0xfffffffffffe1000, &vmo);
  EXPECT_EQ(status, ZX_ERR_OUT_OF_RANGE, "should be too large\n");
  END_TEST;
}

// Helper that tests if all pages in a vmo in the specified range pass the given predicate.
template <typename F>
static bool AllPagesMatch(VmObject* vmo, F pred, uint64_t offset, uint64_t len) {
  bool pred_matches = true;
  zx_status_t status =
      vmo->Lookup(offset, len, [&pred, &pred_matches](uint64_t offset, paddr_t pa) {
        const vm_page_t* p = paddr_to_vm_page(pa);
        if (!pred(p)) {
          pred_matches = false;
          return ZX_ERR_STOP;
        }
        return ZX_ERR_NEXT;
      });
  return status == ZX_OK ? pred_matches : false;
}

static bool PagesInAnyUnswappableQueue(VmObject* vmo, uint64_t offset, uint64_t len) {
  return AllPagesMatch(
      vmo, [](const vm_page_t* p) { return pmm_page_queues()->DebugPageIsAnyUnswappable(p); },
      offset, len);
}

static bool PagesInWiredQueue(VmObject* vmo, uint64_t offset, uint64_t len) {
  return AllPagesMatch(
      vmo, [](const vm_page_t* p) { return pmm_page_queues()->DebugPageIsWired(p); }, offset, len);
}

// Creates a vm object, commits memory.
static bool vmo_commit_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ret = vmo->CommitRange(0, alloc_size);
  ASSERT_EQ(ZX_OK, ret, "committing vm object\n");
  EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), PAGE_SIZE * vmo->AttributedPages(),
            "committing vm object\n");
  EXPECT_TRUE(PagesInAnyUnswappableQueue(vmo.get(), 0, alloc_size));
  END_TEST;
}

// Creates a paged VMO, pins it, and tries operations that should unpin it.
static bool vmo_pin_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kResizable, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  status = vmo->CommitRangePinned(PAGE_SIZE, alloc_size);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status, "pinning out of range\n");
  status = vmo->CommitRangePinned(PAGE_SIZE, 0);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status, "pinning range of len 0\n");

  status = vmo->CommitRangePinned(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "pinning committed range\n");
  EXPECT_TRUE(PagesInWiredQueue(vmo.get(), PAGE_SIZE, 3 * PAGE_SIZE));

  status = vmo->DecommitRange(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
  status = vmo->DecommitRange(PAGE_SIZE, PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
  status = vmo->DecommitRange(3 * PAGE_SIZE, PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");

  vmo->Unpin(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_TRUE(PagesInAnyUnswappableQueue(vmo.get(), PAGE_SIZE, 3 * PAGE_SIZE));

  status = vmo->DecommitRange(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

  status = vmo->CommitRangePinned(PAGE_SIZE, 3 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "pinning committed range\n");
  EXPECT_TRUE(PagesInWiredQueue(vmo.get(), PAGE_SIZE, 3 * PAGE_SIZE));

  status = vmo->Resize(0);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "resizing pinned range\n");

  vmo->Unpin(PAGE_SIZE, 3 * PAGE_SIZE);

  status = vmo->Resize(0);
  EXPECT_EQ(ZX_OK, status, "resizing unpinned range\n");

  END_TEST;
}

// Creates a page VMO and pins the same pages multiple times
static bool vmo_multiple_pin_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  status = vmo->CommitRangePinned(0, alloc_size);
  EXPECT_EQ(ZX_OK, status, "pinning whole range\n");
  EXPECT_TRUE(PagesInWiredQueue(vmo.get(), 0, alloc_size));
  status = vmo->CommitRangePinned(PAGE_SIZE, 4 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "pinning subrange\n");
  EXPECT_TRUE(PagesInWiredQueue(vmo.get(), 0, alloc_size));

  for (unsigned int i = 1; i < VM_PAGE_OBJECT_MAX_PIN_COUNT; ++i) {
    status = vmo->CommitRangePinned(0, PAGE_SIZE);
    EXPECT_EQ(ZX_OK, status, "pinning first page max times\n");
  }
  status = vmo->CommitRangePinned(0, PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, status, "page is pinned too much\n");

  vmo->Unpin(0, alloc_size);
  EXPECT_TRUE(PagesInWiredQueue(vmo.get(), PAGE_SIZE, 4 * PAGE_SIZE));
  EXPECT_TRUE(PagesInAnyUnswappableQueue(vmo.get(), 5 * PAGE_SIZE, alloc_size - 5 * PAGE_SIZE));
  status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting pinned range\n");
  status = vmo->DecommitRange(5 * PAGE_SIZE, alloc_size - 5 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

  vmo->Unpin(PAGE_SIZE, 4 * PAGE_SIZE);
  status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

  for (unsigned int i = 2; i < VM_PAGE_OBJECT_MAX_PIN_COUNT; ++i) {
    vmo->Unpin(0, PAGE_SIZE);
  }
  status = vmo->DecommitRange(0, PAGE_SIZE);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status, "decommitting unpinned range\n");

  vmo->Unpin(0, PAGE_SIZE);
  status = vmo->DecommitRange(0, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "decommitting unpinned range\n");

  END_TEST;
}

// Creates a vm object, commits odd sized memory.
static bool vmo_odd_size_commit_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  static const size_t alloc_size = 15;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ret = vmo->CommitRange(0, alloc_size);
  EXPECT_EQ(ZX_OK, ret, "committing vm object\n");
  EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), PAGE_SIZE * vmo->AttributedPages(),
            "committing vm object\n");
  END_TEST;
}

static bool vmo_create_physical_test() {
  BEGIN_TEST;

  paddr_t pa;
  vm_page_t* vm_page;
  zx_status_t status = pmm_alloc_page(0, &vm_page, &pa);
  uint32_t cache_policy;

  ASSERT_EQ(ZX_OK, status, "vm page allocation\n");
  ASSERT_TRUE(vm_page);

  fbl::RefPtr<VmObjectPhysical> vmo;
  status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");
  cache_policy = vmo->GetMappingCachePolicy();
  EXPECT_EQ(ARCH_MMU_FLAG_UNCACHED, cache_policy, "check initial cache policy");
  EXPECT_TRUE(vmo->is_contiguous(), "check contiguous");

  vmo.reset();
  pmm_free_page(vm_page);

  END_TEST;
}

static bool vmo_physical_pin_test() {
  BEGIN_TEST;

  paddr_t pa;
  vm_page_t* vm_page;
  zx_status_t status = pmm_alloc_page(0, &vm_page, &pa);
  ASSERT_EQ(ZX_OK, status);

  fbl::RefPtr<VmObjectPhysical> vmo;
  status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);

  // Validate we can pin the range.
  EXPECT_EQ(ZX_OK, vmo->CommitRangePinned(0, PAGE_SIZE));

  // Pinning out side should fail.
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, vmo->CommitRangePinned(PAGE_SIZE, PAGE_SIZE));

  // Unpin for physical VMOs does not currently do anything, but still call it to be API correct.
  vmo->Unpin(0, PAGE_SIZE);

  vmo.reset();
  pmm_free_page(vm_page);

  END_TEST;
}

// Creates a vm object that commits contiguous memory.
static bool vmo_create_contiguous_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, alloc_size, 0, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  EXPECT_TRUE(vmo->is_contiguous(), "vmo is contig\n");

  EXPECT_TRUE(PagesInWiredQueue(vmo.get(), 0, alloc_size));

  paddr_t last_pa;
  auto lookup_func = [&last_pa](uint64_t offset, paddr_t pa) {
    if (offset != 0 && last_pa + PAGE_SIZE != pa) {
      return ZX_ERR_BAD_STATE;
    }
    last_pa = pa;
    return ZX_ERR_NEXT;
  };
  status = vmo->Lookup(0, alloc_size, lookup_func);
  paddr_t first_pa;
  paddr_t second_pa;
  EXPECT_EQ(status, ZX_OK, "vmo lookup\n");
  EXPECT_EQ(ZX_OK, vmo->LookupContiguous(0, alloc_size, &first_pa));
  EXPECT_EQ(first_pa + alloc_size - PAGE_SIZE, last_pa);
  EXPECT_EQ(ZX_OK, vmo->LookupContiguous(PAGE_SIZE, PAGE_SIZE, &second_pa));
  EXPECT_EQ(first_pa + PAGE_SIZE, second_pa);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->LookupContiguous(42, PAGE_SIZE, nullptr));
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE,
            vmo->LookupContiguous(alloc_size - PAGE_SIZE, PAGE_SIZE * 2, nullptr));

  END_TEST;
}

// Make sure decommitting is disallowed
static bool vmo_contiguous_decommit_test() {
  BEGIN_TEST;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, alloc_size, 0, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  status = vmo->DecommitRange(PAGE_SIZE, 4 * PAGE_SIZE);
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");
  status = vmo->DecommitRange(0, 4 * PAGE_SIZE);
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");
  status = vmo->DecommitRange(alloc_size - PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED, "decommit fails due to pinned pages\n");

  // Make sure all pages are still present and contiguous
  paddr_t last_pa;
  auto lookup_func = [&last_pa](size_t offset, paddr_t pa) {
    if (offset != 0 && last_pa + PAGE_SIZE != pa) {
      return ZX_ERR_BAD_STATE;
    }
    last_pa = pa;
    return ZX_ERR_NEXT;
  };
  status = vmo->Lookup(0, alloc_size, lookup_func);
  ASSERT_EQ(status, ZX_OK, "vmo lookup\n");

  END_TEST;
}

// Creats a vm object, maps it, precommitted.
static bool vmo_precommitted_map_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                                   kArchRwFlags);
  ASSERT_EQ(ZX_OK, ret, "mapping object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  auto err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");
  END_TEST;
}

// Creates a vm object, maps it, demand paged.
static bool vmo_demand_paged_map_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  auto err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");
  END_TEST;
}

// Creates a vm object, maps it, drops ref before unmapping.
static bool vmo_dropped_ref_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(ktl::move(vmo), "test", 0, alloc_size, &ptr, 0,
                                   VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object");

  EXPECT_NULL(vmo, "dropped ref to object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  auto err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");
  END_TEST;
}

// Creates a vm object, maps it, fills it with data, unmaps,
// maps again somewhere else.
static bool vmo_remap_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                                   kArchRwFlags);
  ASSERT_EQ(ZX_OK, ret, "mapping object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  auto err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");

  // map it again
  ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                              kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object");

  // test that the pattern is still valid
  bool result = test_region((uintptr_t)ptr, ptr, alloc_size);
  EXPECT_TRUE(result, "testing region for corruption");

  err = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, err, "unmapping object");
  END_TEST;
}

// Creates a vm object, maps it, fills it with data, maps it a second time and
// third time somwehere else.
static bool vmo_double_remap_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  auto ret = ka->MapObjectInternal(vmo, "test0", 0, alloc_size, &ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_OK, ret, "mapping object");

  // fill with known pattern and test
  if (!fill_and_test(ptr, alloc_size)) {
    all_ok = false;
  }

  // map it again
  void* ptr2;
  ret = ka->MapObjectInternal(vmo, "test1", 0, alloc_size, &ptr2, 0, 0, kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object second time");
  EXPECT_NE(ptr, ptr2, "second mapping is different");

  // test that the pattern is still valid
  bool result = test_region((uintptr_t)ptr, ptr2, alloc_size);
  EXPECT_TRUE(result, "testing region for corruption");

  // map it a third time with an offset
  void* ptr3;
  static const size_t alloc_offset = PAGE_SIZE;
  ret = ka->MapObjectInternal(vmo, "test2", alloc_offset, alloc_size - alloc_offset, &ptr3, 0, 0,
                              kArchRwFlags);
  ASSERT_EQ(ret, ZX_OK, "mapping object third time");
  EXPECT_NE(ptr3, ptr2, "third mapping is different");
  EXPECT_NE(ptr3, ptr, "third mapping is different");

  // test that the pattern is still valid
  int mc = memcmp((uint8_t*)ptr + alloc_offset, ptr3, alloc_size - alloc_offset);
  EXPECT_EQ(0, mc, "testing region for corruption");

  ret = ka->FreeRegion((vaddr_t)ptr3);
  EXPECT_EQ(ZX_OK, ret, "unmapping object third time");

  ret = ka->FreeRegion((vaddr_t)ptr2);
  EXPECT_EQ(ZX_OK, ret, "unmapping object second time");

  ret = ka->FreeRegion((vaddr_t)ptr);
  EXPECT_EQ(ZX_OK, ret, "unmapping object");
  END_TEST;
}

static bool vmo_read_write_smoke_test() {
  BEGIN_TEST;
  static const size_t alloc_size = PAGE_SIZE * 16;

  // create object
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  // create test buffer
  fbl::AllocChecker ac;
  fbl::Vector<uint8_t> a;
  a.reserve(alloc_size, &ac);
  ASSERT_TRUE(ac.check());
  fill_region(99, a.data(), alloc_size);

  // write to it, make sure it seems to work with valid args
  zx_status_t err = vmo->Write(a.data(), 0, 0);
  EXPECT_EQ(ZX_OK, err, "writing to object");

  err = vmo->Write(a.data(), 0, 37);
  EXPECT_EQ(ZX_OK, err, "writing to object");

  err = vmo->Write(a.data(), 99, 37);
  EXPECT_EQ(ZX_OK, err, "writing to object");

  // can't write past end
  err = vmo->Write(a.data(), 0, alloc_size + 47);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

  // can't write past end
  err = vmo->Write(a.data(), 31, alloc_size + 47);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

  // should return an error because out of range
  err = vmo->Write(a.data(), alloc_size + 99, 42);
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, err, "writing to object");

  // map the object
  auto ka = VmAspace::kernel_aspace();
  uint8_t* ptr;
  err = ka->MapObjectInternal(vmo, "test", 0, alloc_size, (void**)&ptr, 0, 0, kArchRwFlags);
  ASSERT_EQ(ZX_OK, err, "mapping object");

  // write to it at odd offsets
  err = vmo->Write(a.data(), 31, 4197);
  EXPECT_EQ(ZX_OK, err, "writing to object");
  int cmpres = memcmp(ptr + 31, a.data(), 4197);
  EXPECT_EQ(0, cmpres, "reading from object");

  // write to it, filling the object completely
  err = vmo->Write(a.data(), 0, alloc_size);
  EXPECT_EQ(ZX_OK, err, "writing to object");

  // test that the data was actually written to it
  bool result = test_region(99, ptr, alloc_size);
  EXPECT_TRUE(result, "writing to object");

  // unmap it
  ka->FreeRegion((vaddr_t)ptr);

  // test that we can read from it
  fbl::Vector<uint8_t> b;
  b.reserve(alloc_size, &ac);
  ASSERT_TRUE(ac.check(), "can't allocate buffer");

  err = vmo->Read(b.data(), 0, alloc_size);
  EXPECT_EQ(ZX_OK, err, "reading from object");

  // validate the buffer is valid
  cmpres = memcmp(b.data(), a.data(), alloc_size);
  EXPECT_EQ(0, cmpres, "reading from object");

  // read from it at an offset
  err = vmo->Read(b.data(), 31, 4197);
  EXPECT_EQ(ZX_OK, err, "reading from object");
  cmpres = memcmp(b.data(), a.data() + 31, 4197);
  EXPECT_EQ(0, cmpres, "reading from object");
  END_TEST;
}

static bool vmo_cache_test() {
  BEGIN_TEST;

  paddr_t pa;
  vm_page_t* vm_page;
  zx_status_t status = pmm_alloc_page(0, &vm_page, &pa);
  auto ka = VmAspace::kernel_aspace();
  uint32_t cache_policy = ARCH_MMU_FLAG_UNCACHED_DEVICE;
  uint32_t cache_policy_get;
  void* ptr;

  ASSERT_TRUE(vm_page);
  // Test that the flags set/get properly
  {
    fbl::RefPtr<VmObjectPhysical> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    cache_policy_get = vmo->GetMappingCachePolicy();
    EXPECT_NE(cache_policy, cache_policy_get, "check initial cache policy");
    EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "try set");
    cache_policy_get = vmo->GetMappingCachePolicy();
    EXPECT_EQ(cache_policy, cache_policy_get, "compare flags");
  }

  // Test valid flags
  for (uint32_t i = 0; i <= ARCH_MMU_FLAG_CACHE_MASK; i++) {
    fbl::RefPtr<VmObjectPhysical> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "try setting valid flags");
  }

  // Test invalid flags
  for (uint32_t i = ARCH_MMU_FLAG_CACHE_MASK + 1; i < 32; i++) {
    fbl::RefPtr<VmObjectPhysical> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(i), "try set with invalid flags");
  }

  // Test valid flags with invalid flags
  {
    fbl::RefPtr<VmObjectPhysical> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0x5), "bad 0x5");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0xA), "bad 0xA");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0x55), "bad 0x55");
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0xAA), "bad 0xAA");
  }

  // Test that changing policy while mapped is blocked
  {
    fbl::RefPtr<VmObjectPhysical> vmo;
    status = VmObjectPhysical::Create(pa, PAGE_SIZE, &vmo);
    ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
    ASSERT_TRUE(vmo, "vmobject creation\n");
    ASSERT_EQ(ZX_OK,
              ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, (void**)&ptr, 0, 0, kArchRwFlags),
              "map vmo");
    EXPECT_EQ(ZX_ERR_BAD_STATE, vmo->SetMappingCachePolicy(cache_policy), "set flags while mapped");
    EXPECT_EQ(ZX_OK, ka->FreeRegion((vaddr_t)ptr), "unmap vmo");
    EXPECT_EQ(ZX_OK, vmo->SetMappingCachePolicy(cache_policy), "set flags after unmapping");
    ASSERT_EQ(ZX_OK,
              ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, (void**)&ptr, 0, 0, kArchRwFlags),
              "map vmo again");
    EXPECT_EQ(ZX_OK, ka->FreeRegion((vaddr_t)ptr), "unmap vmo");
  }

  pmm_free_page(vm_page);
  END_TEST;
}

static bool vmo_lookup_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  static const size_t alloc_size = PAGE_SIZE * 16;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, alloc_size, &vmo);
  ASSERT_EQ(status, ZX_OK, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  size_t pages_seen = 0;
  auto lookup_fn = [&pages_seen](size_t offset, paddr_t pa) {
    pages_seen++;
    return ZX_ERR_NEXT;
  };
  status = vmo->Lookup(0, alloc_size, lookup_fn);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(0u, pages_seen, "lookup on uncommitted pages\n");
  pages_seen = 0;

  status = vmo->CommitRange(PAGE_SIZE, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status, "committing vm object\n");
  EXPECT_EQ(static_cast<size_t>(1), vmo->AttributedPages(), "committing vm object\n");

  // Should not see any pages in the early range.
  status = vmo->Lookup(0, PAGE_SIZE, lookup_fn);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(0u, pages_seen, "lookup on partially committed pages\n");
  pages_seen = 0;

  // Should see a committed page if looking at any range covering the committed.
  status = vmo->Lookup(0, alloc_size, lookup_fn);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(1u, pages_seen, "lookup on partially committed pages\n");
  pages_seen = 0;

  status = vmo->Lookup(PAGE_SIZE, alloc_size - PAGE_SIZE, lookup_fn);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(1u, pages_seen, "lookup on partially committed pages\n");
  pages_seen = 0;

  status = vmo->Lookup(PAGE_SIZE, PAGE_SIZE, lookup_fn);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(1u, pages_seen, "lookup on partially committed pages\n");
  pages_seen = 0;

  // Contiguous lookups of single pages should also succeed
  status = vmo->LookupContiguous(PAGE_SIZE, PAGE_SIZE, nullptr);
  EXPECT_EQ(ZX_OK, status, "contiguous lookup of single page\n");

  // Commit the rest
  status = vmo->CommitRange(0, alloc_size);
  EXPECT_EQ(ZX_OK, status, "committing vm object\n");
  EXPECT_EQ(alloc_size, PAGE_SIZE * vmo->AttributedPages(), "committing vm object\n");

  status = vmo->Lookup(0, alloc_size, lookup_fn);
  EXPECT_EQ(ZX_OK, status, "lookup on partially committed pages\n");
  EXPECT_EQ(alloc_size / PAGE_SIZE, pages_seen, "lookup on partially committed pages\n");
  status = vmo->LookupContiguous(0, PAGE_SIZE, nullptr);
  EXPECT_EQ(ZX_OK, status, "contiguous lookup of single page\n");
  status = vmo->LookupContiguous(0, alloc_size, nullptr);
  EXPECT_NE(ZX_OK, status, "contiguous lookup of multiple pages\n");

  END_TEST;
}

static bool vmo_lookup_clone_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  static const size_t page_count = 4;
  static const size_t alloc_size = PAGE_SIZE * page_count;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, alloc_size, &vmo);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");
  ASSERT_TRUE(vmo, "vmobject creation\n");

  vmo->set_user_id(ZX_KOID_KERNEL);

  // Commit the whole original VMO and the first and last page of the clone.
  status = vmo->CommitRange(0, alloc_size);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");

  fbl::RefPtr<VmObject> clone;
  status = vmo->CreateClone(Resizability::NonResizable, CloneType::Snapshot, 0, alloc_size, false,
                            &clone);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");
  ASSERT_TRUE(clone, "vmobject creation\n");

  clone->set_user_id(ZX_KOID_KERNEL);

  status = clone->CommitRange(0, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");
  status = clone->CommitRange(alloc_size - PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status, "vmobject creation\n");

  // Lookup the paddrs for both VMOs.
  paddr_t vmo_lookup[page_count] = {};
  paddr_t clone_lookup[page_count] = {};
  auto vmo_lookup_func = [&vmo_lookup](uint64_t offset, paddr_t pa) {
    vmo_lookup[offset / PAGE_SIZE] = pa;
    return ZX_ERR_NEXT;
  };
  auto clone_lookup_func = [&clone_lookup](uint64_t offset, paddr_t pa) {
    clone_lookup[offset / PAGE_SIZE] = pa;
    return ZX_ERR_NEXT;
  };
  status = vmo->Lookup(0, alloc_size, vmo_lookup_func);
  EXPECT_EQ(ZX_OK, status, "vmo lookup\n");
  status = clone->Lookup(0, alloc_size, clone_lookup_func);
  EXPECT_EQ(ZX_OK, status, "vmo lookup\n");

  // The original VMO is now copy-on-write so we should see none of its pages,
  // and we should only see the two pages that explicitly committed into the clone.
  for (unsigned i = 0; i < page_count; i++) {
    EXPECT_EQ(0ul, vmo_lookup[i], "Bad paddr\n");
    if (i == 0 || i == page_count - 1) {
      EXPECT_NE(0ul, clone_lookup[i], "Bad paddr\n");
    }
  }

  END_TEST;
}

static bool vmo_clone_removes_write_test() {
  BEGIN_TEST;

  // Create and map a VMO.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, PAGE_SIZE, &vmo);
  EXPECT_EQ(ZX_OK, status, "vmo create");
  auto ka = VmAspace::kernel_aspace();
  void* ptr;
  status = ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, &ptr, 0, VmAspace::VMM_FLAG_COMMIT,
                                 kArchRwFlags);
  EXPECT_EQ(ZX_OK, status, "map vmo");

  // Query the aspace and validate there is a writable mapping.
  paddr_t paddr_writable;
  uint mmu_flags;
  status = ka->arch_aspace().Query(reinterpret_cast<vaddr_t>(ptr), &paddr_writable, &mmu_flags);
  EXPECT_EQ(ZX_OK, status, "query aspace");

  EXPECT_TRUE(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE, "mapping is writable check");

  // Clone the VMO, which causes the parent to have to downgrade any mappings to read-only so that
  // copy-on-write can take place. Need to set a fake user id so that the COW creation code is
  // happy.
  vmo->set_user_id(42);
  fbl::RefPtr<VmObject> clone;
  status =
      vmo->CreateClone(Resizability::NonResizable, CloneType::Snapshot, 0, PAGE_SIZE, true, &clone);
  EXPECT_EQ(ZX_OK, status, "create clone");

  // Aspace should now have a read only mapping with the same underlying page.
  paddr_t paddr_readable;
  status = ka->arch_aspace().Query(reinterpret_cast<vaddr_t>(ptr), &paddr_readable, &mmu_flags);
  EXPECT_EQ(ZX_OK, status, "query aspace");
  EXPECT_FALSE(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE, "mapping is read only check");
  EXPECT_EQ(paddr_writable, paddr_readable, "mapping has same page");

  // Cleanup.
  status = ka->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
  EXPECT_EQ(ZX_OK, status, "unmapping object");

  END_TEST;
}

static bool vmo_zero_scan_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  auto mem = testing::UserMemory::Create(PAGE_SIZE);
  ASSERT_NONNULL(mem);

  const auto& user_aspace = mem->aspace();
  ASSERT_NONNULL(user_aspace);
  ASSERT_TRUE(user_aspace->is_user());

  // Initially uncommitted, which should not count as having zero pages.
  EXPECT_EQ(0u, mem->vmo()->ScanForZeroPages(false));

  // Validate that this mapping reads as zeros
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mem->base(), 0u));
  EXPECT_EQ(0, mem->get<int32_t>());

  // Reading from the page should not have committed anything, zero or otherwise.
  EXPECT_EQ(0u, mem->vmo()->ScanForZeroPages(false));

  // IF we write to the page, this should make it committed.
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mem->base(), VMM_PF_FLAG_WRITE));
  mem->put<int32_t>(0);
  EXPECT_EQ(1u, mem->vmo()->ScanForZeroPages(false));

  // Check that changing the contents effects the zero page count.
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mem->base(), VMM_PF_FLAG_WRITE));
  mem->put<int32_t>(42);
  EXPECT_EQ(0u, mem->vmo()->ScanForZeroPages(false));
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mem->base(), VMM_PF_FLAG_WRITE));
  mem->put<int32_t>(0);
  EXPECT_EQ(1u, mem->vmo()->ScanForZeroPages(false));

  // Scanning should drop permissions in the hardware page table from write to read-only.
  paddr_t paddr_readable;
  uint mmu_flags;
  EXPECT_EQ(ZX_OK, user_aspace->SoftFault(mem->base(), VMM_PF_FLAG_WRITE));
  mem->put<int32_t>(0);
  zx_status_t status = user_aspace->arch_aspace().Query(mem->base(), &paddr_readable, &mmu_flags);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_TRUE(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE);
  mem->vmo()->ScanForZeroPages(false);
  status = user_aspace->arch_aspace().Query(mem->base(), &paddr_readable, &mmu_flags);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_FALSE(mmu_flags & ARCH_MMU_FLAG_PERM_WRITE);

  // Pinning the page should prevent it from being counted.
  EXPECT_EQ(1u, mem->vmo()->ScanForZeroPages(false));
  EXPECT_EQ(ZX_OK, mem->vmo()->CommitRangePinned(0, PAGE_SIZE));
  EXPECT_EQ(0u, mem->vmo()->ScanForZeroPages(false));
  mem->vmo()->Unpin(0, PAGE_SIZE);
  EXPECT_EQ(1u, mem->vmo()->ScanForZeroPages(false));

  // Creating a kernel mapping should prevent any counting from occurring.
  VmAspace* kernel_aspace = VmAspace::kernel_aspace();
  void* ptr;
  status = kernel_aspace->MapObjectInternal(mem->vmo(), "test", 0, PAGE_SIZE, &ptr, 0,
                                            VmAspace::VMM_FLAG_COMMIT, kArchRwFlags);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(0u, mem->vmo()->ScanForZeroPages(false));
  kernel_aspace->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
  EXPECT_EQ(1u, mem->vmo()->ScanForZeroPages(false));

  END_TEST;
}

static bool vmo_move_pages_on_access_test() {
  BEGIN_TEST;

  AutoVmScannerDisable scanner_disable;

  fbl::RefPtr<VmObjectPaged> vmo;
  vm_page_t* page;
  zx_status_t status = make_committed_pager_vmo(&page, &vmo);
  ASSERT_EQ(ZX_OK, status);

  // Our page should now be in a pager backed page queue.
  EXPECT_TRUE(pmm_page_queues()->DebugPageIsPagerBacked(page));

  PageRequest request;
  // If we lookup the page then it should be moved to specifically the first page queue.
  status = vmo->GetPage(0, VMM_PF_FLAG_SW_FAULT, nullptr, nullptr, nullptr, nullptr);
  EXPECT_EQ(ZX_OK, status);
  size_t queue;
  EXPECT_TRUE(pmm_page_queues()->DebugPageIsPagerBacked(page, &queue));
  EXPECT_EQ(0u, queue);

  // Rotate the queues and check the page moves.
  pmm_page_queues()->RotatePagerBackedQueues();
  EXPECT_TRUE(pmm_page_queues()->DebugPageIsPagerBacked(page, &queue));
  EXPECT_EQ(1u, queue);

  // Touching the page should move it back to the first queue.
  status = vmo->GetPage(0, VMM_PF_FLAG_SW_FAULT, nullptr, nullptr, nullptr, nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_TRUE(pmm_page_queues()->DebugPageIsPagerBacked(page, &queue));
  EXPECT_EQ(0u, queue);

  // Touching pages in a child should also move the page to the front of the queues.
  fbl::RefPtr<VmObject> child;
  status = vmo->CreateClone(Resizability::NonResizable, CloneType::PrivatePagerCopy, 0, PAGE_SIZE,
                            true, &child);
  ASSERT_EQ(ZX_OK, status);

  status = child->GetPage(0, VMM_PF_FLAG_SW_FAULT, nullptr, nullptr, nullptr, nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_TRUE(pmm_page_queues()->DebugPageIsPagerBacked(page, &queue));
  EXPECT_EQ(0u, queue);
  pmm_page_queues()->RotatePagerBackedQueues();
  EXPECT_TRUE(pmm_page_queues()->DebugPageIsPagerBacked(page, &queue));
  EXPECT_EQ(1u, queue);
  status = child->GetPage(0, VMM_PF_FLAG_SW_FAULT, nullptr, nullptr, nullptr, nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_TRUE(pmm_page_queues()->DebugPageIsPagerBacked(page, &queue));
  EXPECT_EQ(0u, queue);

  END_TEST;
}

static bool vmo_eviction_test() {
  BEGIN_TEST;
  // Disable the page scanner as this test would be flaky if our pages get evicted by someone else.
  scanner_push_disable_count();
  auto pop_count = fbl::MakeAutoCall([] { scanner_pop_disable_count(); });

  // Make two pager backed vmos
  fbl::RefPtr<VmObjectPaged> vmo;
  fbl::RefPtr<VmObjectPaged> vmo2;
  vm_page_t* page;
  vm_page_t* page2;
  zx_status_t status = make_committed_pager_vmo(&page, &vmo);
  ASSERT_EQ(ZX_OK, status);
  status = make_committed_pager_vmo(&page2, &vmo2);
  ASSERT_EQ(ZX_OK, status);

  // Shouldn't be able to evict pages from the wrong VMO.
  EXPECT_FALSE(vmo->DebugGetCowPages()->EvictPage(page2, 0));
  EXPECT_FALSE(vmo2->DebugGetCowPages()->EvictPage(page, 0));

  // Eviction should actually drop the number of committed pages.
  EXPECT_EQ(1u, vmo2->AttributedPages());
  EXPECT_TRUE(vmo2->DebugGetCowPages()->EvictPage(page2, 0));
  EXPECT_EQ(0u, vmo2->AttributedPages());
  pmm_free_page(page2);
  EXPECT_GT(vmo2->EvictionEventCount(), 0u);

  // Pinned pages should not be evictable.
  status = vmo->CommitRangePinned(0, PAGE_SIZE);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_FALSE(vmo->DebugGetCowPages()->EvictPage(page, 0));
  vmo->Unpin(0, PAGE_SIZE);

  END_TEST;
}

// This test exists to provide a location for VmObjectPaged::DebugValidatePageSplits to be
// regularly called so that it doesn't bitrot. Additionally it *might* detect VMO object corruption,
// but it's primary goal is to test the implementation of DebugValidatePageSplits
static bool vmo_validate_page_splits_test() {
  BEGIN_TEST;

  zx_status_t status = VmObject::ForEach([](const VmObject& vmo) -> zx_status_t {
    if (vmo.is_paged()) {
      const VmObjectPaged& paged = static_cast<const VmObjectPaged&>(vmo);
      if (!paged.DebugValidatePageSplits()) {
        return ZX_ERR_INTERNAL;
      }
    }
    return ZX_OK;
  });

  // Although DebugValidatePageSplits says to panic as soon as possible if it returns false, this
  // test errs on side of assuming that the validation is broken, and not the hierarchy, and so does
  // not panic. Either way the test still fails, this is just more graceful.
  EXPECT_EQ(ZX_OK, status);

  END_TEST;
}

// Tests that page attribution caching behaves as expected under various cloning behaviors -
// creation of snapshot clones and slices, removal of clones, committing pages in the original vmo
// and in the clones.
static bool vmo_attribution_clones_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, 4 * PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);
  // Dummy user id to keep the cloning code happy.
  vmo->set_user_id(0xff);

  uint64_t expected_gen_count = 1;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  // Commit the first two pages. This should increment the generation count by 2 (one per
  // GetPageLocked() call that results in a page getting committed).
  status = vmo->CommitRange(0, 2 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_gen_count += 2;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 2u));

  // Create a clone that sees the second and third pages.
  fbl::RefPtr<VmObject> clone;
  status = vmo->CreateClone(Resizability::NonResizable, CloneType::Snapshot, PAGE_SIZE,
                            2 * PAGE_SIZE, true, &clone);
  ASSERT_EQ(ZX_OK, status);
  clone->set_user_id(0xfc);

  // Creation of the clone should increment the generation count.
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 2u));
  EXPECT_EQ(true, verify_object_page_attribution(clone.get(), expected_gen_count, 0u));

  // Commit both pages in the clone. This should increment the generation count by the no. of pages
  // committed in the clone.
  status = clone->CommitRange(0, 2 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_gen_count += 2;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 2u));
  EXPECT_EQ(true, verify_object_page_attribution(clone.get(), expected_gen_count, 2u));

  // Commit the last page in the original vmo, which should increment the generation count by 1.
  status = vmo->CommitRange(3 * PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 3u));

  // Create a slice that sees all four pages of the original vmo.
  fbl::RefPtr<VmObject> slice;
  status = vmo->CreateChildSlice(0, 4 * PAGE_SIZE, true, &slice);
  ASSERT_EQ(ZX_OK, status);
  slice->set_user_id(0xf5);

  // Creation of the slice should increment the generation count.
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 3u));
  EXPECT_EQ(true, verify_object_page_attribution(clone.get(), expected_gen_count, 2u));
  EXPECT_EQ(true, verify_object_page_attribution(slice.get(), expected_gen_count, 0u));

  // Committing the slice's last page is a no-op (as the page is already committed) and should *not*
  // increment the generation count.
  status = slice->CommitRange(3 * PAGE_SIZE, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 3u));

  // Committing the remaining 3 pages in the slice will commit pages in the original vmo, and should
  // increment the generation count by 3 (1 per page committed).
  status = slice->CommitRange(0, 4 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_gen_count += 3;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 4u));
  EXPECT_EQ(true, verify_object_page_attribution(clone.get(), expected_gen_count, 2u));
  EXPECT_EQ(true, verify_object_page_attribution(slice.get(), expected_gen_count, 0u));

  // Removing the clone should increment the generation count.
  clone.reset();
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 4u));
  EXPECT_EQ(true, verify_object_page_attribution(slice.get(), expected_gen_count, 0u));

  // Removing the slice should increment the generation count.
  slice.reset();
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 4u));

  END_TEST;
}

// Tests that page attribution caching behaves as expected under various operations performed on the
// vmo that can change its page list - committing / decommitting pages, reading / writing, zero
// range, resizing.
static bool vmo_attribution_ops_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kResizable, 4 * PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  uint64_t expected_gen_count = 1;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  // Committing pages should increment the generation count.
  status = vmo->CommitRange(0, 4 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_gen_count += 4;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 4u));

  // Committing the same range again will be a no-op, and should *not* increment the generation
  // count.
  status = vmo->CommitRange(0, 4 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 4u));

  // Decommitting pages should increment the generation count.
  status = vmo->DecommitRange(0, 4 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  fbl::AllocChecker ac;
  fbl::Vector<uint8_t> buf;
  buf.reserve(2 * PAGE_SIZE, &ac);
  ASSERT_TRUE(ac.check());

  // Read the first two pages. Since these are zero pages being read, this won't commit any pages in
  // the vmo and should not increment the generation count.
  status = vmo->Read(buf.data(), 0, 2 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  // Write the first two pages. This will commit 2 pages and should increment the generation count.
  status = vmo->Write(buf.data(), 0, 2 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_gen_count += 2;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 2u));

  // Resizing the vmo should increment the generation count.
  status = vmo->Resize(2 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 2u));

  // Zero'ing the range will decommit pages, and should increment the generation count.
  status = vmo->ZeroRange(0, 2 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  END_TEST;
}

// Tests that page attribution caching behaves as expected for operations specific to pager-backed
// vmo's - supplying pages, creating COW clones.
static bool vmo_attribution_pager_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  fbl::AllocChecker ac;
  fbl::RefPtr<StubPageSource> pager = fbl::MakeRefCountedChecked<StubPageSource>(&ac);
  ASSERT_TRUE(ac.check());

  static const size_t alloc_size = 2 * PAGE_SIZE;
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::CreateExternal(ktl::move(pager), 0, alloc_size, &vmo);
  ASSERT_EQ(ZX_OK, status);
  // Dummy user id to keep the cloning code happy.
  vmo->set_user_id(0xff);

  uint64_t expected_gen_count = 1;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  // Create an aux VMO to transfer pages into the pager-backed vmo.
  fbl::RefPtr<VmObjectPaged> aux_vmo;
  status =
      VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kResizable, alloc_size, &aux_vmo);
  ASSERT_EQ(ZX_OK, status);

  uint64_t aux_expected_gen_count = 1;
  EXPECT_EQ(true, verify_object_page_attribution(aux_vmo.get(), aux_expected_gen_count, 0u));

  // Committing pages in the aux vmo should increment its generation count.
  status = aux_vmo->CommitRange(0, alloc_size);
  ASSERT_EQ(ZX_OK, status);
  aux_expected_gen_count += 2;
  EXPECT_EQ(true, verify_object_page_attribution(aux_vmo.get(), aux_expected_gen_count, 2u));

  // Taking pages from the aux vmo should increment its generation count.
  VmPageSpliceList page_list;
  status = aux_vmo->TakePages(0, PAGE_SIZE, &page_list);
  ASSERT_EQ(ZX_OK, status);
  ++aux_expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(aux_vmo.get(), aux_expected_gen_count, 1u));
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  // Supplying pages to the pager-backed vmo should increment the generation count.
  status = vmo->SupplyPages(0, PAGE_SIZE, &page_list);
  ASSERT_EQ(ZX_OK, status);
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 1u));
  EXPECT_EQ(true, verify_object_page_attribution(aux_vmo.get(), aux_expected_gen_count, 1u));

  aux_vmo.reset();

  // Create a COW clone that sees the first page.
  fbl::RefPtr<VmObject> clone;
  status = vmo->CreateClone(Resizability::NonResizable, CloneType::PrivatePagerCopy, 0, PAGE_SIZE,
                            true, &clone);
  ASSERT_EQ(ZX_OK, status);
  clone->set_user_id(0xfc);

  // Creation of the clone should increment the generation count.
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 1u));
  EXPECT_EQ(true, verify_object_page_attribution(clone.get(), expected_gen_count, 0u));

  // Committing the clone should increment the generation count.
  status = clone->CommitRange(0, PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 1u));
  EXPECT_EQ(true, verify_object_page_attribution(clone.get(), expected_gen_count, 1u));

  // Removal of the clone should increment the generation count.
  clone.reset();
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 1u));

  END_TEST;
}

// Tests that page attribution caching behaves as expected when a pager-backed vmo's page is
// evicted.
static bool vmo_attribution_evict_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  fbl::RefPtr<VmObjectPaged> vmo;
  vm_page_t* page;
  zx_status_t status = make_committed_pager_vmo(&page, &vmo);
  ASSERT_EQ(ZX_OK, status);

  uint64_t expected_gen_count = 2;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 1u));

  // Evicting the page should increment the generation count.
  vmo->DebugGetCowPages()->EvictPage(page, 0);
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  END_TEST;
}

// Tests that page attribution caching behaves as expected when zero pages are deduped, changing the
// no. of committed pages in the vmo.
static bool vmo_attribution_dedup_test() {
  BEGIN_TEST;
  AutoVmScannerDisable scanner_disable;

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, 2 * PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  uint64_t expected_gen_count = 1;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  // Committing pages should increment the generation count.
  status = vmo->CommitRange(0, 2 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_gen_count += 2;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 2u));

  vm_page_t* page;
  status = vmo->GetPage(0, 0, nullptr, nullptr, &page, nullptr);
  ASSERT_EQ(ZX_OK, status);

  // Dedupe the first page. This should increment the generation count.
  auto vmop = static_cast<VmObjectPaged*>(vmo.get());
  ASSERT_TRUE(vmop->DebugGetCowPages()->DedupZeroPage(page, 0));
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 1u));

  // Dedupe the second page. This should increment the generation count.
  status = vmo->GetPage(PAGE_SIZE, 0, nullptr, nullptr, &page, nullptr);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_TRUE(vmop->DebugGetCowPages()->DedupZeroPage(page, PAGE_SIZE));
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  // Commit the range again.
  status = vmo->CommitRange(0, 2 * PAGE_SIZE);
  ASSERT_EQ(ZX_OK, status);
  expected_gen_count += 2;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 2u));

  // Scan for zero pages, returning only the count (without triggering any reclamation). This should
  // *not* change the generation count.
  ASSERT_EQ(2u, vmo->ScanForZeroPages(false));
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 2u));

  // Scan for zero pages and reclaim them. This should change the generation count.
  ASSERT_EQ(2u, vmo->ScanForZeroPages(true));
  ++expected_gen_count;
  EXPECT_EQ(true, verify_object_page_attribution(vmo.get(), expected_gen_count, 0u));

  END_TEST;
}

// Test that a VmObjectPaged that is only referenced by its children gets removed by effectively
// merging into its parent and re-homing all the children. This should also drop any VmCowPages
// being held open.
static bool vmo_parent_merge_test() {
  BEGIN_TEST;

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);

  // Set a user ID for testing.
  vmo->set_user_id(42);

  fbl::RefPtr<VmObject> child;
  status = vmo->CreateClone(Resizability::NonResizable, CloneType::Snapshot, 0, PAGE_SIZE, false,
                            &child);
  ASSERT_EQ(ZX_OK, status);

  child->set_user_id(43);

  EXPECT_EQ(0u, vmo->parent_user_id());
  EXPECT_EQ(42u, vmo->user_id());
  EXPECT_EQ(43u, child->user_id());
  EXPECT_EQ(42u, child->parent_user_id());

  // Dropping the parent should re-home the child to an empty parent.
  vmo.reset();
  EXPECT_EQ(43u, child->user_id());
  EXPECT_EQ(0u, child->parent_user_id());

  child.reset();

  // Recreate a more interesting 3 level hierarchy with vmo->child->(child2,child3)

  status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, PAGE_SIZE, &vmo);
  ASSERT_EQ(ZX_OK, status);
  vmo->set_user_id(42);
  status = vmo->CreateClone(Resizability::NonResizable, CloneType::Snapshot, 0, PAGE_SIZE, false,
                            &child);
  ASSERT_EQ(ZX_OK, status);
  child->set_user_id(43);
  fbl::RefPtr<VmObject> child2;
  status = child->CreateClone(Resizability::NonResizable, CloneType::Snapshot, 0, PAGE_SIZE, false,
                              &child2);
  ASSERT_EQ(ZX_OK, status);
  child2->set_user_id(44);
  fbl::RefPtr<VmObject> child3;
  status = child->CreateClone(Resizability::NonResizable, CloneType::Snapshot, 0, PAGE_SIZE, false,
                              &child3);
  ASSERT_EQ(ZX_OK, status);
  child3->set_user_id(45);
  EXPECT_EQ(0u, vmo->parent_user_id());
  EXPECT_EQ(42u, child->parent_user_id());
  EXPECT_EQ(43u, child2->parent_user_id());
  EXPECT_EQ(43u, child3->parent_user_id());

  // Drop the intermediate child, child2+3 should get re-homed to vmo
  child.reset();
  EXPECT_EQ(42u, child2->parent_user_id());
  EXPECT_EQ(42u, child3->parent_user_id());

  END_TEST;
}

UNITTEST_START_TESTCASE(vmo_tests)
VM_UNITTEST(vmo_create_test)
VM_UNITTEST(vmo_create_maximum_size)
VM_UNITTEST(vmo_pin_test)
VM_UNITTEST(vmo_multiple_pin_test)
VM_UNITTEST(vmo_commit_test)
VM_UNITTEST(vmo_odd_size_commit_test)
VM_UNITTEST(vmo_create_physical_test)
VM_UNITTEST(vmo_physical_pin_test)
VM_UNITTEST(vmo_create_contiguous_test)
VM_UNITTEST(vmo_contiguous_decommit_test)
VM_UNITTEST(vmo_precommitted_map_test)
VM_UNITTEST(vmo_demand_paged_map_test)
VM_UNITTEST(vmo_dropped_ref_test)
VM_UNITTEST(vmo_remap_test)
VM_UNITTEST(vmo_double_remap_test)
VM_UNITTEST(vmo_read_write_smoke_test)
VM_UNITTEST(vmo_cache_test)
VM_UNITTEST(vmo_lookup_test)
VM_UNITTEST(vmo_lookup_clone_test)
VM_UNITTEST(vmo_clone_removes_write_test)
VM_UNITTEST(vmo_zero_scan_test)
VM_UNITTEST(vmo_move_pages_on_access_test)
VM_UNITTEST(vmo_eviction_test)
VM_UNITTEST(vmo_validate_page_splits_test)
VM_UNITTEST(vmo_attribution_clones_test)
VM_UNITTEST(vmo_attribution_ops_test)
VM_UNITTEST(vmo_attribution_pager_test)
VM_UNITTEST(vmo_attribution_evict_test)
VM_UNITTEST(vmo_attribution_dedup_test)
VM_UNITTEST(vmo_parent_merge_test)
UNITTEST_END_TESTCASE(vmo_tests, "vmo", "VmObject tests")

}  // namespace vm_unittest
