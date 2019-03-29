// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <stdint.h>

#include "spinel_result.h"

//
//
//

spn_result
spn_styling_impl_create(struct spn_device    * const device,
                        struct spn_styling * * const styling,
                        uint32_t               const dwords_count,
                        uint32_t               const layers_count);

//
//
//
