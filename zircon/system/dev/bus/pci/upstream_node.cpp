// Copyright 2018 The Fuchsia Authors
// Copyright (c) 2018, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <fbl/algorithm.h>
#include <fbl/limits.h>
#include <inttypes.h>
#include <string.h>
#include "common.h"
#include "upstream_node.h"

namespace pci {

UpstreamNode::~UpstreamNode() {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
}

zx_status_t UpstreamNode::LinkDevice(pci::Device* device) {
    downstream_list_.push_back(device);
    // TODO(cja) Add to bus list
    return ZX_OK;
}

zx_status_t UpstreamNode::UnlinkDevice(pci::Device* device) {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
    return ZX_OK;
}

void UpstreamNode::AllocateDownstreamBars() {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
}

void UpstreamNode::DisableDownstream() {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
}

void UpstreamNode::UnplugDownstream() {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
}

void UpstreamNode::ScanDownstream() {
    pci_errorf("%s unimplemented!\n", __PRETTY_FUNCTION__);
}

} // namespace pci
