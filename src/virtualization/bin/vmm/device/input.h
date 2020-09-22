// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_INPUT_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_INPUT_H_

#include <stdint.h>

static constexpr uint32_t kButtonTouchCode = 0x14a;

// TODO(fxbug.dev/12526): Use real touch/pen digitizer resolutions.
static constexpr uint32_t kInputAbsMaxX = UINT16_MAX;
static constexpr uint32_t kInputAbsMaxY = UINT16_MAX;

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_INPUT_H_
