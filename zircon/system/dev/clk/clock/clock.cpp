// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

#include "clock.h"

namespace clock {

zx_status_t ClockDevice::ClockEnable(uint32_t index) {
    return clock_.Enable(index);
}

zx_status_t ClockDevice::ClockDisable(uint32_t index) {
    return clock_.Disable(index);
}

void ClockDevice::DdkUnbind() {
    DdkRemove();
}

void ClockDevice::DdkRelease() {
    delete this;
}

zx_status_t ClockDevice::Create(void* ctx, zx_device_t* parent) {
    clock_impl_protocol_t clock_proto;
    auto status = device_get_protocol(parent, ZX_PROTOCOL_CLOCK_IMPL, &clock_proto);
    if (status != ZX_OK) {
        return status;
    }

    size_t metadata_size;
    status = device_get_metadata_size(parent, DEVICE_METADATA_CLOCK_MAPS, &metadata_size);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    std::unique_ptr<uint8_t[]> metadata(new (&ac) uint8_t[metadata_size]);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_CLOCK_MAPS, metadata.get(), metadata_size,
                                 &actual);
    if (status != ZX_OK) {
        return status;
    }
    if (actual != metadata_size) {
        return ZX_ERR_INTERNAL;
    }

    clock_id_maps_t* maps = reinterpret_cast<clock_id_maps_t*>(metadata.get());
    const auto map_count = maps->map_count;
    auto* map = maps->maps;

    for (uint32_t i = 0; i < map_count; i++) {
        // Create an array for the clock map.
        const auto clock_count = map->clock_count;
        fbl::AllocChecker ac;
        fbl::Array<uint32_t> map_array;
        map_array.reset(new (&ac) uint32_t[clock_count], clock_count);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        memcpy(map_array.begin(), map->clock_ids, clock_count * sizeof(map->clock_ids[0]));

        std::unique_ptr<ClockDevice> dev(new (&ac) ClockDevice(parent, &clock_proto,
                                         std::move(map_array)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        char name[20];
        snprintf(name, sizeof(name), "clock-%u", i);
        zx_device_prop_t props[] = {
            { BIND_CHILD_INDEX, 0, i },
        };

        status = dev->DdkAdd(name, 0, props, countof(props));
        if (status != ZX_OK) {
            return status;
        }

        // dev is now owned by devmgr.
        __UNUSED auto ptr = dev.release();

        // Skip to next map.
        map = reinterpret_cast<clock_id_map_t*>(reinterpret_cast<uint8_t*>(map) +
            sizeof(clock_id_map_t) + clock_count * sizeof(map->clock_ids[0]));
    }

    return ZX_OK;
}

static zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = ClockDevice::Create;
    return ops;
}();

} // namespace clock

ZIRCON_DRIVER_BEGIN(clock, clock::driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK_IMPL),
ZIRCON_DRIVER_END(clock)
