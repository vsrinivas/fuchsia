// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "upstream_node.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>

#include "common.h"

namespace pci {

void UpstreamNode::ConfigureDownstreamDevices() {
  for (auto& device : downstream_) {
    // Some capabilities can only be configured after device BARs have been
    // configured, and device BARs cannot be configured when a Device is object
    // is created since bridge windows still need to be allocated.
    if (device.ConfigureBars() != ZX_OK || device.ConfigureCapabilities() != ZX_OK) {
      device.Disable();
    }
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

}  // namespace pci
