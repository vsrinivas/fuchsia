// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_SYSMEM_SYSMEM_H_
#define SRC_DEVICES_LIB_SYSMEM_SYSMEM_H_

#include <fuchsia/sysmem/c/banjo.h>
#include <fuchsia/sysmem/c/fidl.h>

namespace sysmem {

void pixel_format_fidl_from_banjo(const pixel_format_t& source,
                                  fuchsia_sysmem_PixelFormat& destination);
void image_format_2_banjo_from_fidl(const fuchsia_sysmem_ImageFormat_2& source,
                                    image_format_2_t& destination);
void image_format_2_fidl_from_banjo(const image_format_2_t& source,
                                    fuchsia_sysmem_ImageFormat_2& destination);
void buffer_collection_info_2_banjo_from_fidl(const fuchsia_sysmem_BufferCollectionInfo_2& source,
                                              buffer_collection_info_2_t& destination);
void buffer_collection_info_2_fidl_from_banjo(
    const buffer_collection_info_2_t& source,
    fuchsia_sysmem_BufferCollectionInfo_2& destination);

}  // namespace sysmem

#endif  // SRC_DEVICES_LIB_SYSMEM_SYSMEM_H_
