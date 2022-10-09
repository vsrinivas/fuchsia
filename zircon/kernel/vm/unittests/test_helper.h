// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_UNITTESTS_TEST_HELPER_H_
#define ZIRCON_KERNEL_VM_UNITTESTS_TEST_HELPER_H_

#include <align.h>
#include <assert.h>
#include <bits.h>
#include <lib/instrumentation/asan.h>
#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/kernel_aspace.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/vector.h>
#include <kernel/semaphore.h>
#include <ktl/algorithm.h>
#include <ktl/iterator.h>
#include <ktl/move.h>
#include <vm/fault.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/pmm_checker.h>
#include <vm/scanner.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>

#include "../pmm_node.h"

namespace vm_unittest {

constexpr uint kArchRwFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
constexpr uint kArchRwUserFlags = kArchRwFlags | ARCH_MMU_FLAG_PERM_USER;

// Stubbed page provider that is intended to be allowed to create a vmo that believes it is backed
// by a user pager, but is incapable of actually providing pages.
class StubPageProvider : public PageProvider {
 public:
  StubPageProvider(bool trap_dirty = false) : trap_dirty_(trap_dirty) {}
  ~StubPageProvider() override = default;

  const PageSourceProperties& properties() const override { return properties_; }

 private:
  bool GetPageSync(uint64_t offset, VmoDebugInfo vmo_debug_info, vm_page_t** const page_out,
                   paddr_t* const pa_out) override {
    return false;
  }
  void SendAsyncRequest(PageRequest* request) override { panic("Not implemented\n"); }
  void ClearAsyncRequest(PageRequest* request) override { panic("Not implemented\n"); }
  void SwapAsyncRequest(PageRequest* old, PageRequest* new_req) override {
    panic("Not implemented\n");
  }
  bool DebugIsPageOk(vm_page_t* page, uint64_t offset) override { return true; }
  void OnDetach() override {}
  void OnClose() override {}
  zx_status_t WaitOnEvent(Event* event) override { panic("Not implemented\n"); }
  void Dump(uint depth) override {}
  bool SupportsPageRequestType(page_request_type type) const override {
    if (type == page_request_type::READ) {
      return true;
    }
    if (type == page_request_type::DIRTY) {
      return trap_dirty_;
    }
    return false;
  }

  PageSourceProperties properties_{
      .is_user_pager = true,
      .is_preserving_page_content = true,
      .is_providing_specific_physical_pages = false,
      .is_handling_free = false,
  };
  const bool trap_dirty_ = false;
};

// Helper function to allocate memory in a user address space.
zx_status_t AllocUser(VmAspace* aspace, const char* name, size_t size, user_inout_ptr<void>* ptr);

// Create a pager-backed VMO |out_vmo| with size equals |num_pages| pages, and commit all its pages.
// |trap_dirty| controls whether modifications to pages must be trapped in order to generate DIRTY
// page requests. |resizable| controls whether the created VMO is resizable.
// Returns pointers to the pages committed in |out_pages|, so that tests can examine
// their state. Allows tests to work with pager-backed VMOs without blocking on page faults.
zx_status_t make_committed_pager_vmo(size_t num_pages, bool trap_dirty, bool resizable,
                                     vm_page_t** out_pages, fbl::RefPtr<VmObjectPaged>* out_vmo);

// Same as make_committed_pager_vmo but does not commit any pages in the VMO.
zx_status_t make_uncommitted_pager_vmo(size_t num_pages, bool trap_dirty, bool resizable,
                                       fbl::RefPtr<VmObjectPaged>* out_vmo);

uint32_t test_rand(uint32_t seed);

// fill a region of memory with a pattern based on the address of the region
void fill_region(uintptr_t seed, void* _ptr, size_t len);

// just like |fill_region|, but for user memory
void fill_region_user(uintptr_t seed, user_inout_ptr<void> _ptr, size_t len);

// test a region of memory against a known pattern
bool test_region(uintptr_t seed, void* _ptr, size_t len);

// just like |test_region|, but for user memory
bool test_region_user(uintptr_t seed, user_inout_ptr<void> _ptr, size_t len);

bool fill_and_test(void* ptr, size_t len);

// just like |fill_and_test|, but for user memory
bool fill_and_test_user(user_inout_ptr<void> ptr, size_t len);

// Helper function used by the vmo_attribution_* tests.
// Verifies that the current generation count is |vmo_gen| and the current page attribution count is
// |pages|. Also verifies that the cached page attribution has the expected generation and page
// counts after the call to AttributedPages().
bool verify_object_page_attribution(VmObject* vmo, uint64_t vmo_gen,
                                    VmObject::AttributionCounts pages);

// Helper function used by the vm_mapping_attribution_* tests.
// Verifies that the mapping generation count is |mapping_gen| and the current page attribution
// count is |pages|. Also verifies that the cached page attribution has |mapping_gen| as the
// mapping generation count, |vmo_gen| as the VMO generation count and |pages| as the page count
// after the call to AllocatedPages().
bool verify_mapping_page_attribution(VmMapping* mapping, uint64_t mapping_gen, uint64_t vmo_gen,
                                     VmObject::AttributionCounts pages);

// Helper function that internally creates a PageRequest to pass to LookupPages.
zx_status_t vmo_lookup_pages(VmObject* vmo, uint64_t offset, uint pf_flags,
                             VmObject::DirtyTrackingAction mark_dirty, uint64_t max_out_pages,
                             list_node* alloc_list, VmObject::LookupInfo* out);

// Use the function name as the test name
#define VM_UNITTEST(fname) UNITTEST(#fname, fname)

}  // namespace vm_unittest

#endif  // ZIRCON_KERNEL_VM_UNITTESTS_TEST_HELPER_H_
