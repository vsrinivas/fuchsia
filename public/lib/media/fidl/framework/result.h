// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_RESULT_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_RESULT_H_

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

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_RESULT_H_
