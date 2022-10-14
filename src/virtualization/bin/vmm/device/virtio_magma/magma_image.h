// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_MAGMA_IMAGE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_MAGMA_IMAGE_H_

#include <lib/zx/eventpair.h>
#include <lib/zx/vmo.h>

#include <cstdint>

#include "src/graphics/lib/magma/include/magma/magma_common_defs.h"

namespace magma_image {

// Creates a single buffer buffer collection for the given DRM format, and optional
// DRM format modifiers; returns the VMO, scenic import token, and the image parameters,
// including the negotiated format modifier.
magma_status_t CreateDrmImage(uint32_t physical_device_index,
                              const magma_image_create_info_t* create_info,
                              magma_image_info_t* image_info_out, zx::vmo* vmo_out,
                              zx::eventpair* token_out);

}  // namespace magma_image

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_MAGMA_IMAGE_H_
