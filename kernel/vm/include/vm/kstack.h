// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <err.h>
#include <fbl/ref_ptr.h>

class VmAddressRegion;
class VmMapping;

// allocate a kernel stack with appropriate overrun padding
zx_status_t vm_allocate_kstack(bool unsafe, void** kstack_top_out,
                               fbl::RefPtr<VmMapping>* out_kstack_mapping,
                               fbl::RefPtr<VmAddressRegion>* out_kstack_vmar);

// free the stack by dropping refs to the mapping and vmar
zx_status_t vm_free_kstack(fbl::RefPtr<VmMapping>* mapping, fbl::RefPtr<VmAddressRegion>* vmar);
