// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_ID_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_ID_H_

#include <cstdint>

namespace scheduling {

// ID used to schedule an update on a FrameScheduler client. Each client is assumed to have a
// globally and temporally unique SessionId.
using SessionId = uint64_t;

// Value 0 reserved as invalid.
constexpr scheduling::SessionId INVALID_SESSION_ID = 0u;

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_ID_H_
