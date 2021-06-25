
// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_STREAM_ID_H_
#define SRC_UI_SCENIC_LIB_INPUT_STREAM_ID_H_

#include <stdint.h>

namespace scenic_impl::input {

// TODO(fxbug.dev/fxbug.dev/73600): Rename all instances of "stream" to "interaction".
using StreamId = uint32_t;
constexpr StreamId kInvalidStreamId = 0;
StreamId NewStreamId();

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_STREAM_ID_H_
