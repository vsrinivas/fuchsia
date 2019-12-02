// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FORMAT_CONVERSION_FORMAT_CONVERSION_H_
#define SRC_CAMERA_LIB_FORMAT_CONVERSION_FORMAT_CONVERSION_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/image-format/image_format.h>

namespace camera {

fuchsia_sysmem_ImageFormat_2 ConvertHlcppImageFormat2toCType(
    fuchsia::sysmem::ImageFormat_2* hlcpp_image_format2);

void ConvertToOldCTypeBufferCollectionInfo(
    const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection,
    fuchsia_sysmem_BufferCollectionInfo* old_buffer_collection);

void ConvertToCTypeBufferCollectionInfo2(
    const fuchsia::sysmem::BufferCollectionInfo_2& hlcpp_buffer_collection,
    fuchsia_sysmem_BufferCollectionInfo_2* buffer_collection);

}  // namespace camera

#endif  // SRC_CAMERA_LIB_FORMAT_CONVERSION_FORMAT_CONVERSION_H_
