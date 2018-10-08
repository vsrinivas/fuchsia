// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_DEVICE_INPUT_H_
#define GARNET_LIB_MACHINA_DEVICE_INPUT_H_

#include <stdint.h>

namespace machina {

static constexpr uint32_t kButtonTouchCode = 0x14a;

// TODO(MAC-164): Use real touch/pen digitizer resolutions.
static constexpr uint32_t kInputAbsMaxX = UINT16_MAX;
static constexpr uint32_t kInputAbsMaxY = UINT16_MAX;

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_DEVICE_INPUT_H_
