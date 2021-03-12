// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/annotation_map.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace forensics {
namespace crash_reports {
namespace {

using testing::Pair;
using testing::UnorderedElementsAreArray;

TEST(AnnotationMap, SetsCorrectly) {
  AnnotationMap annotations;
  annotations.Set("string", "value")
      .Set("int", 5)
      .Set("true", true)
      .Set("false", false)
      .Set("error-or-value", ErrorOr<std::string>("value"))
      .Set("error-or-error", ErrorOr<std::string>(Error::kMissingValue))
      .Set("error", Error::kMissingValue)
      .Set(::fuchsia::feedback::Annotation{.key = "annotation", .value = "value"});

  EXPECT_THAT(annotations.Raw(), UnorderedElementsAreArray({
                                     Pair("string", "value"),
                                     Pair("int", "5"),
                                     Pair("true", "true"),
                                     Pair("false", "false"),
                                     Pair("error-or-value", "value"),
                                     Pair("error-or-error", "unknown"),
                                     Pair("debug.error-or-error.error", "missing"),
                                     Pair("error", "missing"),
                                     Pair("annotation", "value"),
                                 }));
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
