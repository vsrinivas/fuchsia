// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_MATCHER_RESULT_H_
#define ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_MATCHER_RESULT_H_

#include <cstdlib>

namespace devicetree {

// Actual directives used to communicate the result type.
enum class MatcherResult {
  // Matcher cannot do further progress in the current path.
  kAvoidSubtree,

  // Matcher needs nodes in the current path, so it wishes to visit offspring.
  kVisitSubtree,

  // Matcher has finished collecting information, no more scans are needed.
  kDone,
};

}  // namespace devicetree

#endif  // ZIRCON_KERNEL_LIB_DEVICETREE_INCLUDE_LIB_DEVICETREE_MATCHER_RESULT_H_
