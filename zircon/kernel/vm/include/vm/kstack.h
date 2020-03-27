// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_KSTACK_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_KSTACK_H_

#include <err.h>
#include <sys/types.h>

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

  vaddr_t base() const { return base_; }
  size_t size() const { return size_; }
  vaddr_t top() const { return base_ + size_; }
#if __has_feature(safe_stack)
  vaddr_t unsafe_base() const { return unsafe_base_; }
  vaddr_t unsafe_top() const { return unsafe_base_ + size_; }
#endif
#if __has_feature(shadow_call_stack)
  vaddr_t shadow_call_base() const { return shadow_call_base_; }
  vaddr_t shadow_call_top() const { return shadow_call_base_ + size_; }
#endif

 private:
  vaddr_t base_ = 0;
  size_t size_ = 0;
  fbl::RefPtr<VmAddressRegion> vmar_;

#if __has_feature(safe_stack)
  vaddr_t unsafe_base_ = 0;
  fbl::RefPtr<VmAddressRegion> unsafe_vmar_;
#endif

#if __has_feature(shadow_call_stack)
  vaddr_t shadow_call_base_ = 0;
  fbl::RefPtr<VmAddressRegion> shadow_call_vmar_;
#endif
};

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_KSTACK_H_
