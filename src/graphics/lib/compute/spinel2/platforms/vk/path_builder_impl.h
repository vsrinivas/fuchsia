// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_PATH_BUILDER_IMPL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_PATH_BUILDER_IMPL_H_

//
//
//

#include "device.h"
#include "path_builder.h"

//
//
//

spinel_result_t
spinel_path_builder_impl_create(struct spinel_device *        device,
                                struct spinel_path_builder ** path_builder);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_PATH_BUILDER_IMPL_H_
