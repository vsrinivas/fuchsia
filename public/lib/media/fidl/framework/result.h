// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_RESULT_H_
#define SERVICES_MEDIA_FRAMEWORK_RESULT_H_

#include <cstdint>

namespace mojo {
namespace media {

// Possible result values indicating success or type of failure.
enum class Result {
  kOk,
  kUnknownError,
  kInternalError,
  kUnsupportedOperation,
  kInvalidArgument,
  kNotFound
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_RESULT_H_
