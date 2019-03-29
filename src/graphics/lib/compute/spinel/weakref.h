// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include "spinel_types.h"

//
//
//

typedef uint64_t spn_weakref_epoch_t;

//
//
//

void
spn_weakref_epoch_init(spn_weakref_epoch_t * const epoch_p);

void
spn_weakref_epoch_bump(spn_weakref_epoch_t * const epoch_p);

//
//
//

void
spn_weakref_init(spn_weakref_t * const weakref_p);

void
spn_weakref_update(spn_weakref_t     * const weakref_p,
                   spn_weakref_epoch_t const epoch,
                   uint32_t            const index);

bool
spn_weakref_get_index(spn_weakref_t const * const weakref_p,
                      spn_weakref_epoch_t   const epoch,
                      uint32_t            * const idx_p);

//
//
//
