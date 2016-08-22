// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_STAGES_UTIL_H_
#define SERVICES_MEDIA_FRAMEWORK_STAGES_UTIL_H_

#include <vector>

#include "services/media/framework/stages/stage.h"

namespace mojo {
namespace media {

bool HasPositiveDemand(const std::vector<Output>& outputs);

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_STAGES_UTIL_H_
