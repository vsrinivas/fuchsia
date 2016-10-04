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

void pcie_add_ref_device(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    __UNUSED int result = atomic_add(&dev->ref_count, 1);
    DEBUG_ASSERT(result >= 1);
}

void pcie_release_device(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);

    int result = atomic_add(&dev->ref_count, -1);
    DEBUG_ASSERT(result >= 1);

    /* If that was the last ref, go ahead and free the device memory (after
     * running some sanity checks) */
    if (result == 1) {
        /* We should already be unlinked from the bus's device tree. */
        DEBUG_ASSERT(!dev->upstream);
        DEBUG_ASSERT(!dev->plugged_in);

        /* Any driver we have been associated with should be long gone at this
         * point */
        DEBUG_ASSERT(!dev->started);
        DEBUG_ASSERT(!dev->driver);
        DEBUG_ASSERT(!dev->driver_ctx);

        /* TODO(johngro) : assert that this device's interrupts are disabled at
         * the hardware level and that it is no longer participating in any of
         * the bus driver's shared dispatching. */

        /* TODO(johngro) : Return any allocated BAR regions to the central pool. */

        /* Free the memory associated with this device.  If this is a bridge,
         * also sanity check to make sure that all child devices have been
         * released as well. */
        pcie_bridge_state_t* bridge = pcie_downcast_to_bridge(dev);
        if (bridge) {
#if LK_DEBUGLEVEL > 0
            for (size_t i = 0; i < countof(bridge->downstream); ++i)
                DEBUG_ASSERT(!bridge->downstream[i]);
#endif
            free(bridge);
        } else {
            free(dev);
        }
    }
}

void pcie_link_device_to_upstream(pcie_device_state_t* dev, pcie_bridge_state_t* bridge) {
    DEBUG_ASSERT(dev && bridge);

    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;
    mutex_acquire(&bus_drv->bus_topology_lock);

    /* Hold a reference to our upstream bridge. */
    DEBUG_ASSERT(!dev->upstream);
    dev->upstream = bridge;
    pcie_add_ref_device(&dev->upstream->dev);

    /* Have our upstream bridge hold a reference to us. */
    uint ndx = (dev->dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev->func_id;
    DEBUG_ASSERT(ndx < countof(dev->upstream->downstream));
    DEBUG_ASSERT(!dev->upstream->downstream[ndx]);

    dev->upstream->downstream[ndx] = dev;
    pcie_add_ref_device(dev->upstream->downstream[ndx]);

    mutex_release(&bus_drv->bus_topology_lock);
}

void pcie_unlink_device_from_upstream(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    pcie_bus_driver_state_t* bus_drv = dev->bus_drv;

    mutex_acquire(&bus_drv->bus_topology_lock);
    if (dev->upstream) {
        uint ndx = (dev->dev_id * PCIE_MAX_FUNCTIONS_PER_DEVICE) + dev->func_id;
        DEBUG_ASSERT(ndx < countof(dev->upstream->downstream));
        DEBUG_ASSERT(dev == dev->upstream->downstream[ndx]);

        /* Let go of our parent's reference to ourselves */
        pcie_release_device(dev->upstream->downstream[ndx]);
        dev->upstream->downstream[ndx] = NULL;

        /* Let go of our reference to our parent */
        pcie_release_device(&dev->upstream->dev);
        dev->upstream = NULL;
    }
    mutex_release(&bus_drv->bus_topology_lock);
}

pcie_bridge_state_t* pcie_get_refed_upstream(pcie_device_state_t* dev) {
    DEBUG_ASSERT(dev);
    pcie_bridge_state_t* ret;

    mutex_acquire(&dev->bus_drv->bus_topology_lock);
    ret = dev->upstream;
    if (ret)
        pcie_add_ref_device(&ret->dev);
    mutex_release(&dev->bus_drv->bus_topology_lock);

    return ret;
}

pcie_device_state_t* pcie_get_refed_downstream(pcie_bridge_state_t* bridge, uint ndx) {
    DEBUG_ASSERT(bridge);
    DEBUG_ASSERT(ndx <= countof(bridge->downstream));
    pcie_device_state_t* ret;

    mutex_acquire(&bridge->dev.bus_drv->bus_topology_lock);
    ret = bridge->downstream[ndx];
    if (ret)
        pcie_add_ref_device(ret);
    mutex_release(&bridge->dev.bus_drv->bus_topology_lock);

    return ret;
}

static bool pcie_foreach_device_on_bridge(pcie_bridge_state_t*    bridge,
                                          uint                    level,
                                          pcie_foreach_device_cbk cbk,
                                          void*                   ctx) {
    DEBUG_ASSERT(bridge && cbk);
    pcie_bus_driver_state_t* drv = bridge->dev.bus_drv;
    bool keep_going = true;

    for (size_t i = 0; keep_going && (i < countof(bridge->downstream)); ++i) {
        mutex_acquire(&drv->bus_topology_lock);
        pcie_device_state_t* dev = bridge->downstream[i];
        if (dev)
            pcie_add_ref_device(dev);
        mutex_release(&drv->bus_topology_lock);

        if (!dev)
            continue;

        keep_going = cbk(dev, ctx, level);

        if (keep_going) {
            pcie_bridge_state_t* downstream_bridge = pcie_downcast_to_bridge(dev);
            if (downstream_bridge)
                keep_going = pcie_foreach_device_on_bridge(downstream_bridge, level + 1, cbk, ctx);
        }

        pcie_release_device(dev);
    }

    return keep_going;
}

void pcie_foreach_device(pcie_bus_driver_state_t* drv,
                         pcie_foreach_device_cbk  cbk,
                         void*                    ctx) {
    DEBUG_ASSERT(drv && cbk);

    mutex_acquire(&drv->bus_topology_lock);
    pcie_bridge_state_t* host_bridge = drv->host_bridge;
    if (host_bridge)
        pcie_add_ref_device(&host_bridge->dev);
    mutex_release(&drv->bus_topology_lock);

    if (!host_bridge) {
        printf("No host bridge discovered...\n");
        return;
    }

    if (cbk(&host_bridge->dev, ctx, 0))
        pcie_foreach_device_on_bridge(host_bridge, 0, cbk, ctx);

    pcie_release_device(&host_bridge->dev);
}

typedef struct pcie_get_refed_device_state {
    uint bus_id;
    uint dev_id;
    uint func_id;
    pcie_device_state_t* ret;
} pcie_get_refed_device_state_t;

static bool pcie_get_refed_device_helper(struct pcie_device_state* dev, void* ctx, uint level) {
    DEBUG_ASSERT(dev && ctx);
    pcie_get_refed_device_state_t* state = (pcie_get_refed_device_state_t*)ctx;

    if ((state->bus_id  == dev->bus_id) &&
        (state->dev_id  == dev->dev_id) &&
        (state->func_id == dev->func_id)) {
        state->ret = dev;
        pcie_add_ref_device(state->ret);
        return false;
    }

    return true;
}

pcie_device_state_t* pcie_get_refed_device(struct pcie_bus_driver_state* drv,
                                           uint bus_id, uint dev_id, uint func_id) {
    pcie_get_refed_device_state_t state = {
        .bus_id  = bus_id,
        .dev_id  = dev_id,
        .func_id = func_id,
        .ret     = NULL,
    };

    pcie_foreach_device(pcie_get_bus_driver_state(), pcie_get_refed_device_helper, &state);

    return state.ret;
}
