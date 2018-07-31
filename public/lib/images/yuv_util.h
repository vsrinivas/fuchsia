// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_IMAGES_YUV_UTIL_H_
#define LIB_IMAGES_YUV_UTIL_H_

#include <stdint.h>

namespace yuv_util {

void YuvToBgra(uint8_t y_raw, uint8_t u_raw, uint8_t v_raw, uint8_t* bgra);

}  // namespace yuv_util

#endif  // LIB_IMAGES_YUV_UTIL_H_
