// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/composer/ops.fidl.h"

#include <ostream>

namespace mozart {
namespace composer {

std::ostream& operator<<(std::ostream& stream, const mozart2::OpPtr& op);
std::ostream& operator<<(std::ostream& stream,
                         const mozart2::CreateResourceOpPtr& op);

}  // namespace composer
}  // namespace mozart
