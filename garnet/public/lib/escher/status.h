// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_STATUS_H_
#define LIB_ESCHER_STATUS_H_

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

#endif  // LIB_ESCHER_STATUS_H_
