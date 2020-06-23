// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_GMATCHERS_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_GMATCHERS_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"

namespace forensics {
namespace internal {

// Compares two Attachment objects.
template <typename ResultListenerT>
bool DoAttachmentMatch(const fuchsia::feedback::Attachment& actual, const std::string& expected_key,
                       const std::string& expected_value, ResultListenerT* result_listener) {
  if (actual.key != expected_key) {
    *result_listener << "Expected key " << expected_key << ", got " << actual.key;
    return false;
  }

  std::string actual_value;
  if (!fsl::StringFromVmo(actual.value, &actual_value)) {
    *result_listener << "Cannot parse actual VMO for key " << actual.key << " to string";
    return false;
  }

  if (actual_value != expected_value) {
    *result_listener << "Expected value " << expected_value << ", got " << actual_value;
    return false;
  }

  return true;
}

// Compares two Annotation objects.
template <typename ResultListenerT>
bool DoAnnotationMatch(const fuchsia::feedback::Annotation& actual, const std::string& expected_key,
                       const std::string& expected_value, ResultListenerT* result_listener) {
  if (actual.key != expected_key) {
    *result_listener << "Expected key " << expected_key << ", got " << actual.key;
    return false;
  }

  if (actual.value != expected_value) {
    *result_listener << "Expected value " << expected_value << ", got " << actual.value;
    return false;
  }

  return true;
}

template <typename ResultListenerT>
bool DoStringBufferMatch(const fuchsia::mem::Buffer& actual, const std::string& expected,
                         ResultListenerT* result_listener) {
  std::string actual_value;
  if (!fsl::StringFromVmo(actual, &actual_value)) {
    *result_listener << "Cannot parse actual VMO to string";
    return false;
  }

  if (actual_value != expected) {
    return false;
  }

  return true;
}

}  // namespace internal

// Returns true if gMock |arg|.key matches |expected_key|.
MATCHER_P(MatchesKey, expected_key,
          "matches an element with key '" + std::string(expected_key) + "'") {
  return arg.key == expected_key;
}

// Returns true if gMock |arg|.key matches |expected_key| and str(|arg|.value) matches
// |expected_value|, assuming two Attachment objects.
MATCHER_P2(MatchesAttachment, expected_key, expected_value,
           "matches an attachment with key '" + std::string(expected_key) + "' and value '" +
               std::string(expected_value) + "'") {
  return internal::DoAttachmentMatch(arg, expected_key, expected_value, result_listener);
}

// Returns true if gMock |arg|.key matches |expected_key| and str(|arg|.value) matches
// |expected_value|, assuming two Annotation objects.
MATCHER_P2(MatchesAnnotation, expected_key, expected_value,
           "matches an annotation with key '" + std::string(expected_key) + "' and value '" +
               std::string(expected_value) + "'") {
  return internal::DoAnnotationMatch(arg, expected_key, expected_value, result_listener);
}

// Returns true if gMock str(|arg|) matches |expected|.
MATCHER_P(MatchesStringBuffer, expected, "'" + std::string(expected) + "'") {
  return internal::DoStringBufferMatch(arg, expected, result_listener);
}

MATCHER(HasValue, negation ? "is error" : "has value") {
  if (arg.HasValue()) {
    return true;
  }

  return false;
}

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_GMATCHERS_H_
