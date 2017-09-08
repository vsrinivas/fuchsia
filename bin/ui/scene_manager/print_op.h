// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/scenic/ops.fidl.h"
#include "apps/mozart/services/scenic/types.fidl.h"

#include <ostream>

namespace scene_manager {

std::ostream& operator<<(std::ostream& stream, const scenic::OpPtr& op);
std::ostream& operator<<(std::ostream& stream,
                         const scenic::CreateResourceOpPtr& op);
std::ostream& operator<<(std::ostream& stream, const scenic::Value::Tag& tag);

}  // namespace scene_manager
