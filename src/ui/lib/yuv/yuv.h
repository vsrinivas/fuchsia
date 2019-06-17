// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_YUV_YUV_H_
#define SRC_UI_LIB_YUV_YUV_H_

#include <stdint.h>

namespace yuv {

void YuvToBgra(uint8_t y_raw, uint8_t u_raw, uint8_t v_raw, uint8_t* bgra);

}  // namespace yuv

#endif  // SRC_UI_LIB_YUV_YUV_H_
