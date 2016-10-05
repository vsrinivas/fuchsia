// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <magenta/compiler.h>
#include <debug.h>
#include <kernel/mutex.h>
#include <trace.h>

#include "pcie_priv.h"

#define LOCAL_TRACE 0

void pcie_link_device_to_upstream(const mxtl::RefPtr<pcie_device_state_t>& dev,
                                  const mxtl::RefPtr<pcie_bridge_state_t>& bridge) {
    DEBUG_ASSERT(dev && bridge);

    mutex_acquire(&dev->bus_drv.bus_topology_lock);

    /* Hold a reference to our upstream bridge. */
    DEBUG_ASSERT(!dev->upstream);
    dev->upstream = bridge;

    /* Have our upstream bridge hold a reference to us. */
    uint ndx = (dev->dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev->func_id;
    DEBUG_ASSERT(ndx < countof(dev->upstream->downstream));
    DEBUG_ASSERT(!dev->upstream->downstream[ndx]);
    dev->upstream->downstream[ndx] = dev;

    mutex_release(&dev->bus_drv.bus_topology_lock);
}

void pcie_unlink_device_from_upstream(const mxtl::RefPtr<pcie_device_state_t>& dev) {
    DEBUG_ASSERT(dev);

    mutex_acquire(&dev->bus_drv.bus_topology_lock);
    if (dev->upstream) {
        uint ndx = (dev->dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev->func_id;
        DEBUG_ASSERT(ndx < countof(dev->upstream->downstream));
        DEBUG_ASSERT(dev == dev->upstream->downstream[ndx]);

        /* Let go of our parent's reference to ourselves */
        dev->upstream->downstream[ndx] = nullptr;

        /* Let go of our reference to our parent */
        dev->upstream = nullptr;
    }
    mutex_release(&dev->bus_drv.bus_topology_lock);
}

mxtl::RefPtr<pcie_bridge_state_t> pcie_device_state_t::GetUpstream() {
    mutex_acquire(&bus_drv.bus_topology_lock);
    auto ret = upstream;
    mutex_release(&bus_drv.bus_topology_lock);
    return ret;
}

mxtl::RefPtr<pcie_device_state_t> pcie_bridge_state_t::GetDownstream(uint ndx) {
    DEBUG_ASSERT(ndx <= countof(downstream));

    mutex_acquire(&bus_drv.bus_topology_lock);
    auto ret = downstream[ndx];
    mutex_release(&bus_drv.bus_topology_lock);

    return ret;
}

static bool pcie_foreach_device_on_bridge(const mxtl::RefPtr<pcie_bridge_state_t>& bridge,
                                          uint                                     level,
                                          pcie_foreach_device_cbk                  cbk,
                                          void*                                    ctx) {
    DEBUG_ASSERT(bridge && cbk);
    bool keep_going = true;

    for (size_t i = 0; keep_going && (i < countof(bridge->downstream)); ++i) {
        mutex_acquire(&bridge->bus_drv.bus_topology_lock);
        auto dev = bridge->downstream[i];
        mutex_release(&bridge->bus_drv.bus_topology_lock);

        if (!dev)
            continue;

        keep_going = cbk(dev, ctx, level);

        if (keep_going) {
            auto downstream_bridge = dev->DowncastToBridge();
            if (downstream_bridge)
                keep_going = pcie_foreach_device_on_bridge(downstream_bridge, level + 1, cbk, ctx);
        }
    }

    return keep_going;
}

void pcie_foreach_device(pcie_bus_driver_state_t& drv,
                         pcie_foreach_device_cbk  cbk,
                         void*                    ctx) {
    DEBUG_ASSERT(cbk);

    // Grab a reference to the root complex if we can
    mutex_acquire(&drv.bus_topology_lock);
    auto root_complex = drv.root_complex;
    mutex_release(&drv.bus_topology_lock);

    if (root_complex == nullptr) {
        TRACEF("No root complex!");
        return;
    }

    pcie_foreach_device_on_bridge(root_complex, 0, cbk, ctx);
}

typedef struct pcie_get_refed_device_state {
    uint bus_id;
    uint dev_id;
    uint func_id;
    mxtl::RefPtr<pcie_device_state_t> ret;
} pcie_get_refed_device_state_t;

static bool pcie_get_refed_device_helper(const mxtl::RefPtr<pcie_device_state_t>& dev, void* ctx, uint level) {
    DEBUG_ASSERT(dev && ctx);
    pcie_get_refed_device_state_t* state = (pcie_get_refed_device_state_t*)ctx;

    if ((state->bus_id  == dev->bus_id) &&
        (state->dev_id  == dev->dev_id) &&
        (state->func_id == dev->func_id)) {
        state->ret = dev;
        return false;
    }

    return true;
}

mxtl::RefPtr<pcie_device_state_t> pcie_get_refed_device(pcie_bus_driver_state_t& drv,
                                                        uint bus_id, uint dev_id, uint func_id) {
    pcie_get_refed_device_state_t state = {
        .bus_id  = bus_id,
        .dev_id  = dev_id,
        .func_id = func_id,
        .ret     = nullptr,
    };

    pcie_foreach_device(drv, pcie_get_refed_device_helper, &state);

    return mxtl::move(state.ret);
}
