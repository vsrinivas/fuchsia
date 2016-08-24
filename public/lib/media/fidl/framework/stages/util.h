// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_UTIL_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_UTIL_H_

#include <vector>

#include "apps/media/services/framework/stages/stage.h"

namespace mojo {
namespace media {

bool HasPositiveDemand(const std::vector<Output>& outputs);

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_STAGES_UTIL_H_
