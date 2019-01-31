// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/pcie_root.h>

#define LOCAL_TRACE 0

PcieRoot::PcieRoot(PcieBusDriver& bus_drv, uint mbus_id)
    : PcieUpstreamNode(bus_drv, PcieUpstreamNode::Type::ROOT, mbus_id),
      bus_drv_(bus_drv) {
}
