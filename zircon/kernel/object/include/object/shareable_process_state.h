// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SHAREABLE_PROCESS_STATE_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SHAREABLE_PROCESS_STATE_H_

#include <fbl/ref_counted.h>
#include <kernel/mutex.h>
#include <object/futex_context.h>
#include <object/handle_table.h>
#include <vm/vm_aspace.h>

// This class is logically private to ProcessDispatcher.
//
// |ShareableProcessState| contains all the state that can belong to more than one process.
//
// This class is logically private to ProcessDispatcher.
//
// The objects contained in this class have lifetimes that are decoupled from their lifecycle.
class ShareableProcessState : public fbl::RefCounted<ShareableProcessState> {
 public:
  ShareableProcessState() = default;
  ~ShareableProcessState() { DEBUG_ASSERT(process_count_.load() == 0); }

  // Shares this state with a process, effectively incrementing the number of calls to
  // |RemoveFromProcess| that can be made before the shared resources are cleaned up.
  //
  // Returns the previous share count.
  uint32_t IncrementShareCount() {
    const uint32_t prev = process_count_.fetch_add(1);
    return prev;
  }

  // Removes this state from a process. If the state is not shared with any other processes, the
  // shared resources are cleaned.
  void DecrementShareCount() {
    const uint32_t prev = process_count_.fetch_sub(1);
    DEBUG_ASSERT(prev > 0);
    if (prev > 1) {
      return;
    }
    // That was the last one.
    handle_table_.Clean();
    if (aspace_) {
      zx_status_t result = aspace_->Destroy();
      ASSERT_MSG(result == ZX_OK, "%d\n", result);
    }
  }

  bool Initialize(vaddr_t aspace_base, vaddr_t aspace_size, const char* aspace_name) {
    aspace_ = VmAspace::Create(aspace_base, aspace_size, VmAspace::Type::User, aspace_name);
    return aspace_ != nullptr;
  }

  HandleTable& handle_table() { return handle_table_; }
  const HandleTable& handle_table() const { return handle_table_; }

  FutexContext& futex_context() { return futex_context_; }
  const FutexContext& futex_context() const { return futex_context_; }

  fbl::RefPtr<VmAspace> aspace() { return aspace_; }
  const fbl::RefPtr<VmAspace>& aspace() const { return aspace_; }

 private:
  // The number of processes currently sharing this state.
  RelaxedAtomic<uint32_t> process_count_ = 0;

  HandleTable handle_table_;
  FutexContext futex_context_;

  fbl::RefPtr<VmAspace> aspace_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_SHAREABLE_PROCESS_STATE_H_
