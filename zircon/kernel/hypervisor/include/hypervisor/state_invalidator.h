// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_STATE_INVALIDATOR_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_STATE_INVALIDATOR_H_

namespace hypervisor {

// Allows hypervisor state to be invalidated.
struct StateInvalidator {
  virtual ~StateInvalidator() = default;
  virtual void Invalidate() = 0;
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_STATE_INVALIDATOR_H_
