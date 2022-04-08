// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_VPID_ALLOCATOR_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_VPID_ALLOCATOR_H_

#include <hypervisor/id_allocator.h>
#include <kernel/mutex.h>

namespace hypervisor {

template <typename T, T N>
class VpidAllocator {
 public:
  zx::status<T> Alloc() {
    Guard<Mutex> lock{&mutex_};
    return allocator_.Alloc();
  }

  zx::status<> Free(T vpid) {
    Guard<Mutex> lock{&mutex_};
    return allocator_.Free(vpid);
  }

 private:
  DECLARE_MUTEX(VpidAllocator) mutex_;
  IdAllocator<T, N> TA_GUARDED(mutex_) allocator_;
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_VPID_ALLOCATOR_H_
