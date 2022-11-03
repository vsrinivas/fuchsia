// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FORMAT_CONVERSION_FORMAT_CONVERSION_H_
#define SRC_CAMERA_LIB_FORMAT_CONVERSION_FORMAT_CONVERSION_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fuchsia/sysmem/cpp/fidl.h>

namespace camera {

fuchsia_sysmem::wire::ImageFormat2 ConvertHlcppImageFormat2toWireType(
    const fuchsia::sysmem::ImageFormat_2& hlcpp_image_format2);

fuchsia_sysmem::wire::BufferCollectionInfo2 ConvertToWireTypeBufferCollectionInfo2(
    fuchsia::sysmem::BufferCollectionInfo_2& hlcpp_buffer_collection);

fuchsia_sysmem::wire::ImageFormat2 GetImageFormatFromBufferCollection(
    const fuchsia_sysmem::wire::BufferCollectionInfo2& buffer_collection, uint32_t coded_width,
    uint32_t coded_height);

fuchsia_sysmem::wire::PixelFormat ConvertPixelFormatToWire(fuchsia::sysmem::PixelFormat format);

}  // namespace camera

#endif  // SRC_CAMERA_LIB_FORMAT_CONVERSION_FORMAT_CONVERSION_H_
