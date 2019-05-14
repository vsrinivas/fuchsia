// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_PATH_BUILDER_IMPL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_PATH_BUILDER_IMPL_H_

//
//
//

#include "path_builder.h"

//
//
//

struct spn_device;

//
//
//

spn_result
spn_path_builder_impl_create(struct spn_device * const        device,
                             struct spn_path_builder ** const path_builder);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_PLATFORMS_VK_PATH_BUILDER_IMPL_H_
