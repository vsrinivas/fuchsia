// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/interrupt/arm_gic_common.h>

#include <assert.h>

zx_status_t gic_register_sgi_handler(unsigned int vector, int_handler handler, void* arg) {
    DEBUG_ASSERT(vector < GIC_BASE_PPI);
    return register_int_handler(vector, handler, arg);
}
