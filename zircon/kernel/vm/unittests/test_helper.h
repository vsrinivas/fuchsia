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
#include <fbl/auto_call.h>
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

class TestPageRequest {
 public:
  TestPageRequest(PmmNode* node, uint64_t off, uint64_t len)
      : node_(node), request_({off, len, pages_available_cb, drop_ref_cb, this, {}}) {}

  ~TestPageRequest() {
    ASSERT(drop_ref_evt_.Wait(Deadline::no_slack(ZX_TIME_INFINITE_PAST)) == ZX_OK);
  }

  void WaitForAvailable(uint64_t* expected_off, uint64_t* expected_len, uint64_t* actual_supplied);

  bool Cancel();

  page_request_t* request() { return &request_; }
  Event& drop_ref_evt() { return drop_ref_evt_; }
  list_node* page_list() { return &page_list_; }
  Event& on_pages_avail_evt() { return on_pages_avail_evt_; }

 private:
  void OnPagesAvailable(uint64_t offset, uint64_t count, uint64_t* actual_supplied);

  void OnDropRef() { drop_ref_evt_.Signal(); }

  PmmNode* node_;
  page_request_t request_;

  list_node page_list_ = LIST_INITIAL_VALUE(page_list_);

  Semaphore wait_for_avail_sem_;
  Semaphore avail_sem_;
  Event on_pages_avail_evt_;
  uint64_t* expected_off_;
  uint64_t* expected_len_;
  uint64_t* actual_supplied_;

  Event drop_ref_evt_;

  static void pages_available_cb(void* ctx, uint64_t offset, uint64_t count,
                                 uint64_t* actual_supplied) {
    static_cast<TestPageRequest*>(ctx)->OnPagesAvailable(offset, count, actual_supplied);
  }
  static void drop_ref_cb(void* ctx) { static_cast<TestPageRequest*>(ctx)->OnDropRef(); }
};

// Stubbed page source that is intended to be allowed to create a vmo that believes it is backed by
// a user pager, but is incapable of actually providing pages.
class StubPageSource : public PageSource {
 public:
  StubPageSource() = default;
  ~StubPageSource() = default;
  bool GetPage(uint64_t offset, VmoDebugInfo vmo_debug_info, vm_page_t** const page_out,
               paddr_t* const pa_out) override {
    return false;
  }
  void GetPageAsync(page_request_t* request) override { panic("Not implemented\n"); }
  void ClearAsyncRequest(page_request_t* request) override { panic("Not implemented\n"); }
  void SwapRequest(page_request_t* old, page_request_t* new_req) override {
    panic("Not implemented\n");
  }
  void OnDetach() override {}
  void OnClose() override {}
  zx_status_t WaitOnEvent(Event* event) override { panic("Not implemented\n"); }
};

// Helper function to allocate memory in a user address space.
zx_status_t AllocUser(VmAspace* aspace, const char* name, size_t size, user_inout_ptr<void>* ptr);

zx_status_t make_committed_pager_vmo(vm_page_t** out_page, fbl::RefPtr<VmObjectPaged>* out_vmo);

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
bool verify_object_page_attribution(VmObject* vmo, uint64_t vmo_gen, size_t pages);

// Helper function used by the vm_mapping_attribution_* tests.
// Verifies that the mapping generation count is |mapping_gen| and the current page attribution
// count is |pages|. Also verifies that the cached page attribution has |mapping_gen| as the
// mapping generation count, |vmo_gen| as the VMO generation count and |pages| as the page count
// after the call to AllocatedPages().
bool verify_mapping_page_attribution(VmMapping* mapping, uint64_t mapping_gen, uint64_t vmo_gen,
                                     size_t pages);

// Use the function name as the test name
#define VM_UNITTEST(fname) UNITTEST(#fname, fname)

}  // namespace vm_unittest

#endif  // ZIRCON_KERNEL_VM_UNITTESTS_TEST_HELPER_H_
