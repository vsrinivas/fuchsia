// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>
#include <list.h>
#include <lk/init.h>
#include <mxtl/limits.h>
#include <new.h>
#include <dev/interrupt.h>
#include <string.h>
#include <trace.h>
#include <platform.h>

#include <dev/pcie_root.h>

#define LOCAL_TRACE 0

PcieRoot::PcieRoot(PcieBusDriver& bus_drv, uint mbus_id)
    : PcieUpstreamNode(bus_drv, PcieUpstreamNode::Type::ROOT, mbus_id),
      bus_drv_(bus_drv) {
}

mxtl::RefPtr<PcieRoot> PcieRoot::Create(PcieBusDriver& bus_drv, uint managed_bus_id) {
    AllocChecker ac;

    auto root = mxtl::AdoptRef(new (&ac) PcieRoot(bus_drv, managed_bus_id));
    if (!ac.check()) {
        TRACEF("Out of memory attemping to create PCIe root for bus 0x%02x\n",
                managed_bus_id);
        return nullptr;
    }

    return root;
}

