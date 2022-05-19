// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/encode.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/feedback/cpp/fidl.h"
#include "src/developer/forensics/feedback/annotations/types.h"

namespace fuchsia::feedback {

bool operator==(const Annotation& lhs, const Annotation& rhs) {
  return lhs.key == rhs.key && lhs.value == rhs.value;
}

}  // namespace fuchsia::feedback

namespace forensics::feedback {
namespace {

TEST(EncodeTest, AnnotationsAsFidl) {
  const auto annotations = Encode<fuchsia::feedback::Annotations>(Annotations({
      {"key1", "value1"},
      {"key2", "value2"},
      {"key3", Error::kTimeout},
  }));

  const std::vector<fuchsia::feedback::Annotation> expected({
      fuchsia::feedback::Annotation{.key = "key1", .value = "value1"},
      fuchsia::feedback::Annotation{.key = "key2", .value = "value2"},
  });

  ASSERT_TRUE(annotations.has_annotations());
  EXPECT_EQ(annotations.annotations(), expected);
}

TEST(EncodeTest, EmptyAnnotationsAsFidl) {
  const auto annotations = Encode<fuchsia::feedback::Annotations>(Annotations({}));

  EXPECT_FALSE(annotations.has_annotations());
}

TEST(EncodeTest, AnnotationsAsString) {
  EXPECT_EQ(Encode<std::string>(Annotations({
                {"key1", "value1"},
                {"key2", "value2"},
                {"key3", Error::kTimeout},
            })),
            R"({
    "key1": "value1",
    "key2": "value2"
})");
}

TEST(EncodeTest, EmptyAnnotationsAsString) {
  EXPECT_EQ(Encode<std::string>(Annotations({})), "{}");
}

}  // namespace
}  // namespace forensics::feedback
