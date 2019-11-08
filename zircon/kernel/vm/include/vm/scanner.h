// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_

// Trigger the memory scanner to perform a single pass. This function returns immediately and the
// scanning will actually happen on a different thread and be reported on the console. Multiple
// triggers will get coalesced together until at least one scanner pass has completed.
// The `reclaim` flag controls whether the scan will perform reclamation, or just report on what
// could be reclaimed.
void scanner_trigger_scan(bool reclaim);

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_SCANNER_H_
