// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_UTIL_FORMATTING_H_
#define SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_UTIL_FORMATTING_H_

#include <lib/zx/time.h>

#include <ostream>

#include "src/ui/lib/glm_workaround/glm_workaround.h"

namespace std {

std::ostream& operator<<(std::ostream& o, const glm::vec2& v);
std::ostream& operator<<(std::ostream& o, const zx::duration& d);

}  // namespace std

#endif  // SRC_UI_A11Y_LIB_MAGNIFIER_TESTS_UTIL_FORMATTING_H_
