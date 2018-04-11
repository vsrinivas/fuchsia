// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lk/init.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef void (*lk_pdev_init_hook)(const void* driver_data, uint32_t length);

// for registering platform drivers
struct lk_pdev_init_struct {
    uint32_t type;          // driver type, as defined in <zircon/boot/kernel-drivers.h>
    lk_pdev_init_hook hook; // hook for driver init
    uint level;             // init level for the hook
    const char *name;
};

#define LK_PDEV_INIT(_name, _type, _hook, _level) \
    __ALIGNED(sizeof(void *)) __USED __SECTION(".data.rel.ro.lk_pdev_init") \
    static const struct lk_pdev_init_struct _dev_init_struct_##_name = { \
        .type = _type, \
        .hook = _hook, \
        .level = _level, \
        .name = #_name, \
    };

__END_CDECLS
