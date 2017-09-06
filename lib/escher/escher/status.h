// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace escher {

enum class Status {
  kOk,
  kNotReady,
  kTimeout,
  kOutOfHostMemory,
  kOutOfDeviceMemory,
  kDeviceLost,
  kInternalError  // should not occur
};

}  // namespace escher
