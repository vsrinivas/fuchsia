// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lk/init.h>
#include <magenta/compiler.h>
#include <magenta/mdi.h>
#include <mdi/mdi.h>

__BEGIN_CDECLS

typedef void (*lk_pdev_init_hook)(mdi_node_ref_t* node, uint level);

// for registering platform drivers
struct lk_pdev_init_struct {
    mdi_id_t id;            // id for driver's config info in MDI
    lk_pdev_init_hook hook; // hook for driver init
    uint level;             // init level for the hook
    const char *name;
};

#define LK_PDEV_INIT(_name, _id, _hook, _level) \
    __ALIGNED(sizeof(void *)) __USED __SECTION("lk_pdev_init") \
    static const struct lk_pdev_init_struct _dev_init_struct_##_name = { \
        .id = _id, \
        .hook = _hook, \
        .level = _level, \
        .name = #_name, \
    };

__END_CDECLS
