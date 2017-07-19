// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/scene/ops.fidl.h"
#include "apps/mozart/services/scene/types.fidl.h"

#include <ostream>

namespace mozart {
namespace scene {

std::ostream& operator<<(std::ostream& stream, const mozart2::OpPtr& op);
std::ostream& operator<<(std::ostream& stream,
                         const mozart2::CreateResourceOpPtr& op);
std::ostream& operator<<(std::ostream& stream, const mozart2::Value::Tag& tag);

}  // namespace scene
}  // namespace mozart
