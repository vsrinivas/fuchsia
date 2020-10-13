// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_SPINWAIT_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_SPINWAIT_H_

#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace wlan {
namespace brcmfmac {

// Perform a spinwait with a total timeout of `timeout` and a wait interval of `interval`.  The
// conditional in `conditional` is evaluated at every iteration, and is expected to return a
// zx_status_t.  If this value is:
//
// * ZX_OK, the spinwait returns ZX_OK.
// * ZX_ERR_NEXT, the spinwait will perform the next iteration.
// * Another other value, the spinwait will return that value.
template <typename Conditional>
zx_status_t Spinwait(zx::duration interval, zx::duration timeout, Conditional conditional) {
  zx_status_t status = ZX_OK;

  const int max_spin_count = timeout / interval;
  int spin_count = 0;
  while ((status = conditional()) != ZX_OK) {
    if (status != ZX_ERR_NEXT) {
      return status;
    }
    if (spin_count >= max_spin_count) {
      return ZX_ERR_TIMED_OUT;
    }
    ++spin_count;
    zx::nanosleep(zx::deadline_after(interval));
  }

  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CHIPSET_SPINWAIT_H_
