// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/magnifier/tests/util/formatting.h"

#include <iomanip>

#include <glm/gtx/string_cast.hpp>

namespace std {

std::ostream& operator<<(std::ostream& o, const glm::vec2& v) { return o << glm::to_string(v); }

std::ostream& operator<<(std::ostream& o, const zx::duration& d) {
  return o << d.to_secs() << "." << std::setfill('0') << std::setw(3) << d.to_msecs() % 1000 << "s";
}

}  // namespace std
