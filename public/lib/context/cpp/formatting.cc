// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/lib/context/formatting.h"

namespace maxwell {
namespace context {

std::ostream& operator<<(std::ostream& os, const Update& o) {
  return os << "{ source: " << o.source << ", json_value: " << o.json_value
            << "}";
}

}  // namespace context
}  // namespace maxwell
