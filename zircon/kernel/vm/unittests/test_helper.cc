// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "test_helper.h"

namespace vm_unittest {

void TestPageRequest::WaitForAvailable(uint64_t* expected_off, uint64_t* expected_len,
                                       uint64_t* actual_supplied) {
  expected_off_ = expected_off;
  expected_len_ = expected_len;
  actual_supplied_ = actual_supplied;
  avail_sem_.Post();

  wait_for_avail_sem_.Wait(Deadline::infinite());
}

bool TestPageRequest::Cancel() {
  bool res = node_->ClearRequest(&request_);
  actual_supplied_ = nullptr;
  avail_sem_.Post();
  return res;
}

void TestPageRequest::OnPagesAvailable(uint64_t offset, uint64_t count, uint64_t* actual_supplied) {
  on_pages_avail_evt_.Signal();
  avail_sem_.Wait(Deadline::infinite());

  if (actual_supplied_) {
    *expected_off_ = offset;
    *expected_len_ = count;
    *actual_supplied = 0;

    while (count) {
      vm_page_t* page;
      zx_status_t status = node_->AllocPage(PMM_ALLOC_DELAY_OK, &page, nullptr);
      if (status != ZX_OK) {
        break;
      }

      count--;
      *actual_supplied += 1;
      list_add_tail(&page_list_, &page->queue_node);
    }
    *actual_supplied_ = *actual_supplied;
  } else {
    *actual_supplied = count;
  }

  wait_for_avail_sem_.Post();
  on_pages_avail_evt_.Unsignal();
}

// Helper function to allocate memory in a user address space.
zx_status_t AllocUser(VmAspace* aspace, const char* name, size_t size, user_inout_ptr<void>* ptr) {
  ASSERT(aspace->is_user());

  size = ROUNDUP(size, PAGE_SIZE);
  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, size, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  vmo->set_name(name, strlen(name));
  static constexpr const uint kArchFlags = kArchRwFlags | ARCH_MMU_FLAG_PERM_USER;
  fbl::RefPtr<VmMapping> mapping;
  status = aspace->RootVmar()->CreateVmMapping(0, size, 0, 0, vmo, 0, kArchFlags, name, &mapping);
  if (status != ZX_OK) {
    return status;
  }

  *ptr = make_user_inout_ptr(reinterpret_cast<void*>(mapping->base()));
  return ZX_OK;
}

zx_status_t make_committed_pager_vmo(vm_page_t** out_page, fbl::RefPtr<VmObjectPaged>* out_vmo) {
  // Create a pager backed VMO and jump through some hoops to pre-fill a page for it so we do not
  // actually take any page faults.
  fbl::AllocChecker ac;
  fbl::RefPtr<StubPageSource> pager = fbl::MakeRefCountedChecked<StubPageSource>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::CreateExternal(ktl::move(pager), 0, PAGE_SIZE, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  VmPageList pl;
  pl.InitializeSkew(0, 0);
  vm_page_t* page;
  status = pmm_alloc_page(0, &page);
  if (status != ZX_OK) {
    return status;
  }
  page->set_state(VM_PAGE_STATE_OBJECT);
  VmPageOrMarker* page_or_marker = pl.LookupOrAllocate(0);
  if (!page_or_marker) {
    return ZX_ERR_NO_MEMORY;
  }
  *page_or_marker = VmPageOrMarker::Page(page);
  VmPageSpliceList splice_list = pl.TakePages(0, PAGE_SIZE);
  status = vmo->SupplyPages(0, PAGE_SIZE, &splice_list);
  if (status != ZX_OK) {
    return status;
  }
  *out_page = page;
  *out_vmo = ktl::move(vmo);
  return ZX_OK;
}

uint32_t test_rand(uint32_t seed) { return (seed = seed * 1664525 + 1013904223); }

// fill a region of memory with a pattern based on the address of the region
void fill_region(uintptr_t seed, void* _ptr, size_t len) {
  uint32_t* ptr = (uint32_t*)_ptr;

  ASSERT(IS_ALIGNED((uintptr_t)ptr, 4));

  uint32_t val = (uint32_t)seed;
  val ^= (uint32_t)(seed >> 32);
  for (size_t i = 0; i < len / 4; i++) {
    ptr[i] = val;

    val = test_rand(val);
  }
}

// just like |fill_region|, but for user memory
void fill_region_user(uintptr_t seed, user_inout_ptr<void> _ptr, size_t len) {
  user_inout_ptr<uint32_t> ptr = _ptr.reinterpret<uint32_t>();

  ASSERT(IS_ALIGNED(ptr.get(), 4));

  uint32_t val = (uint32_t)seed;
  val ^= (uint32_t)(seed >> 32);
  for (size_t i = 0; i < len / 4; i++) {
    zx_status_t status = ptr.element_offset(i).copy_to_user(val);
    ASSERT(status == ZX_OK);

    val = test_rand(val);
  }
}

// test a region of memory against a known pattern
bool test_region(uintptr_t seed, void* _ptr, size_t len) {
  uint32_t* ptr = (uint32_t*)_ptr;

  ASSERT(IS_ALIGNED((uintptr_t)ptr, 4));

  uint32_t val = (uint32_t)seed;
  val ^= (uint32_t)(seed >> 32);
  for (size_t i = 0; i < len / 4; i++) {
    if (ptr[i] != val) {
      unittest_printf("value at %p (%zu) is incorrect: 0x%x vs 0x%x\n", &ptr[i], i, ptr[i], val);
      return false;
    }

    val = test_rand(val);
  }

  return true;
}

// just like |test_region|, but for user memory
bool test_region_user(uintptr_t seed, user_inout_ptr<void> _ptr, size_t len) {
  user_inout_ptr<uint32_t> ptr = _ptr.reinterpret<uint32_t>();

  ASSERT(IS_ALIGNED(ptr.get(), 4));

  uint32_t val = (uint32_t)seed;
  val ^= (uint32_t)(seed >> 32);
  for (size_t i = 0; i < len / 4; i++) {
    auto p = ptr.element_offset(i);
    uint32_t actual;
    zx_status_t status = p.copy_from_user(&actual);
    ASSERT(status == ZX_OK);
    if (actual != val) {
      unittest_printf("value at %p (%zu) is incorrect: 0x%x vs 0x%x\n", p.get(), i, actual, val);
      return false;
    }

    val = test_rand(val);
  }

  return true;
}

bool fill_and_test(void* ptr, size_t len) {
  BEGIN_TEST;

  // fill it with a pattern
  fill_region((uintptr_t)ptr, ptr, len);

  // test that the pattern is read back properly
  auto result = test_region((uintptr_t)ptr, ptr, len);
  EXPECT_TRUE(result, "testing region for corruption");

  END_TEST;
}

// just like |fill_and_test|, but for user memory
bool fill_and_test_user(user_inout_ptr<void> ptr, size_t len) {
  BEGIN_TEST;

  const auto seed = reinterpret_cast<uintptr_t>(ptr.get());

  // fill it with a pattern
  fill_region_user(seed, ptr, len);

  // test that the pattern is read back properly
  auto result = test_region_user(seed, ptr, len);
  EXPECT_TRUE(result, "testing region for corruption");

  END_TEST;
}

// Helper function used by the vmo_attribution_* tests.
// Verifies that the current generation count is |vmo_gen| and the current page attribution count is
// |pages|. Also verifies that the cached page attribution has the expected generation and page
// counts after the call to AttributedPages().
bool verify_object_page_attribution(VmObject* vmo, uint64_t vmo_gen, size_t pages) {
  BEGIN_TEST;

  auto vmo_paged = static_cast<VmObjectPaged*>(vmo);
  EXPECT_EQ(vmo_gen, vmo_paged->GetHierarchyGenerationCount());

  EXPECT_EQ(pages, vmo->AttributedPages());

  VmObjectPaged::CachedPageAttribution attr = vmo_paged->GetCachedPageAttribution();
  EXPECT_EQ(vmo_gen, attr.generation_count);
  EXPECT_EQ(pages, attr.page_count);

  END_TEST;
}

// Helper function used by the vm_mapping_attribution_* tests.
// Verifies that the mapping generation count is |mapping_gen| and the current page attribution
// count is |pages|. Also verifies that the cached page attribution has |mapping_gen| as the
// mapping generation count, |vmo_gen| as the VMO generation count and |pages| as the page count
// after the call to AllocatedPages().
bool verify_mapping_page_attribution(VmMapping* mapping, uint64_t mapping_gen, uint64_t vmo_gen,
                                     size_t pages) {
  BEGIN_TEST;

  EXPECT_EQ(mapping_gen, mapping->GetMappingGenerationCount());

  EXPECT_EQ(pages, mapping->AllocatedPages());

  VmMapping::CachedPageAttribution attr = mapping->GetCachedPageAttribution();
  EXPECT_EQ(mapping_gen, attr.mapping_generation_count);
  EXPECT_EQ(vmo_gen, attr.vmo_generation_count);
  EXPECT_EQ(pages, attr.page_count);

  END_TEST;
}

}  // namespace vm_unittest
