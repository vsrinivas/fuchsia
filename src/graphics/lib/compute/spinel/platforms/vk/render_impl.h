// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include "context.h"

//
//
//

struct spn_device;

//
//
//

spn_result
spn_render_impl(struct spn_device         * const device,
                spn_render_submit_t const * const submit);

//
//
//
