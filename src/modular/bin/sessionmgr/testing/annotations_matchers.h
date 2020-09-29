// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_TESTING_ANNOTATIONS_MATCHERS_H_
#define SRC_MODULAR_BIN_SESSIONMGR_TESTING_ANNOTATIONS_MATCHERS_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/session/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>

#include "lib/fostr/fidl/fuchsia/modular/formatting.h"
#include "src/lib/fsl/vmo/strings.h"

namespace modular::annotations {

using Annotation = fuchsia::modular::Annotation;

template <typename ResultListenerT>
bool IsAnnotationEq(const Annotation& actual, const Annotation& expected,
                    ResultListenerT* result_listener) {
  if (actual.key != expected.key) {
    *result_listener << "Expected key " << expected.key << ", got " << actual.key;
    return false;
  }

  // Compare buffers by their contents, not strict equality as fidl::Equals does.
  if (actual.value && actual.value->is_buffer() && expected.value && expected.value->is_buffer()) {
    std::string actual_str;
    std::string expected_str;
    FX_DCHECK(fsl::StringFromVmo(actual.value->buffer(), &actual_str));
    FX_DCHECK(fsl::StringFromVmo(expected.value->buffer(), &expected_str));

    if (actual_str != expected_str) {
      *result_listener << "Expected value " << expected.value << ", got " << actual.value;
      return false;
    }

    return true;
  }

  if (!fidl::Equals(actual.value, expected.value)) {
    *result_listener << "Expected value " << expected.value << ", got " << actual.value;
    return false;
  }

  return true;
}

MATCHER_P(AnnotationEq, expected, "annotation equals") {
  return IsAnnotationEq(arg, expected, result_listener);
}

}  // namespace modular::annotations

namespace session::annotations {

using Annotation = fuchsia::session::Annotation;

template <typename ResultListenerT>
bool IsAnnotationEq(const Annotation& actual, const Annotation& expected,
                    ResultListenerT* result_listener) {
  if (actual.key != expected.key) {
    *result_listener << "Expected key " << expected.key << ", got " << actual.key;
    return false;
  }

  // Compare buffers by their contents, not strict equality as fidl::Equals does.
  if (actual.value && actual.value->is_buffer() && expected.value && expected.value->is_buffer()) {
    std::string actual_str;
    std::string expected_str;
    FX_DCHECK(fsl::StringFromVmo(actual.value->buffer(), &actual_str));
    FX_DCHECK(fsl::StringFromVmo(expected.value->buffer(), &expected_str));

    if (actual_str != expected_str) {
      *result_listener << "Expected value " << expected.value << ", got " << actual.value;
      return false;
    }

    return true;
  }

  if (!fidl::Equals(actual.value, expected.value)) {
    *result_listener << "Expected value " << expected.value << ", got " << actual.value;
    return false;
  }

  return true;
}

MATCHER_P(AnnotationEq, expected, "annotation equals") {
  return IsAnnotationEq(arg, expected, result_listener);
}

}  // namespace session::annotations

#endif  // SRC_MODULAR_BIN_SESSIONMGR_TESTING_ANNOTATIONS_MATCHERS_H_
