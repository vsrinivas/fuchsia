// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_KSTACK_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_KSTACK_H_

#include <sys/types.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>

class VmAddressRegion;

// KernelStack encapsulates a kernel stack.
// A kernel stack object is not valid until Init() has been successfully
// called.
class KernelStack {
 public:
  KernelStack() = default;

  // Destruction will automatically call Teardown();
  ~KernelStack();

  // Disallow copy; allow move.
  KernelStack(KernelStack&) = delete;
  KernelStack& operator=(KernelStack&) = delete;
  KernelStack(KernelStack&&) = default;
  KernelStack& operator=(KernelStack&&) = default;

  // Initializes a kernel stack with appropriate overrun padding.
  zx_status_t Init();

  // Logs the relevant stack memory addresses at the given debug level.
  // This is useful during a thread dump.
  void DumpInfo(int debug_level);

  // Returns the stack to its pre-Init() state.
  zx_status_t Teardown();

  vaddr_t base() const { return main_map_.base_; }
  size_t size() const { return main_map_.size_; }
  vaddr_t top() const { return main_map_.top(); }
#if __has_feature(safe_stack)
  vaddr_t unsafe_base() const { return unsafe_map_.base_; }
  vaddr_t unsafe_top() const { return unsafe_map_.top(); }
#endif
#if __has_feature(shadow_call_stack)
  vaddr_t shadow_call_base() const { return shadow_call_map_.base_; }
  vaddr_t shadow_call_top() const { return shadow_call_map_.top(); }
#endif

  // Holds the relevant metadata and pointers for an individual mapping
  struct Mapping {
    vaddr_t base_ = 0;
    size_t size_ = 0;
    fbl::RefPtr<VmAddressRegion> vmar_;

    vaddr_t top() const { return base_ + size_; }
  };

 private:
  Mapping main_map_;

#if __has_feature(safe_stack)
  Mapping unsafe_map_;
#endif

#if __has_feature(shadow_call_stack)
  Mapping shadow_call_map_;
#endif
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_KSTACK_H_
