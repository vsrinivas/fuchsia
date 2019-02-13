// Copyright 2018 The Fuchsia Authors
// Copyright (c) 2018, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "upstream_node.h"
#include "common.h"
#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <fbl/limits.h>
#include <inttypes.h>
#include <string.h>

namespace pci {

void UpstreamNode::AllocateDownstreamBars() {
    for (auto& device : downstream_) {
        device.AllocateBars();
    }
}

void UpstreamNode::DisableDownstream() {
    for (auto& device : downstream_) {
        device.Disable();
    }
}

void UpstreamNode::UnplugDownstream() {
    // Unplug our downstream devices and clear them out of the topology.  They
    // will remove themselves from the bus list and their resources will be
    // cleaned up.
    size_t idx = downstream_.size_slow();
    while (!downstream_.is_empty()) {
        // Catch if we've iterated longer than we should have without needing a
        // stable iterator while devices are removing themselves.
        ZX_DEBUG_ASSERT(idx-- > 0);
        // Hold a device reference to ensure the dtor fires after unplug has
        // finished.
        fbl::RefPtr<Device> dev = fbl::RefPtr(&downstream_.front());
        dev->Unplug();
    }
}

} // namespace pci
