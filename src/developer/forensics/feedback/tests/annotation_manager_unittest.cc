// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {
namespace {

using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

constexpr bool kIsMissingNonPlatform = true;
constexpr bool kNotIsMissingNonPlatform = false;

auto MakePair(const char* key, const char* value) { return Pair(key, ErrorOr<std::string>(value)); }
auto MakePair(const char* key, const Error error) { return Pair(key, ErrorOr<std::string>(error)); }

class DynamicNonPlatform : public NonPlatformAnnotationProvider {
 public:
  DynamicNonPlatform(const bool exempt = kNotIsMissingNonPlatform)
      : is_missing_annotations_(exempt) {}

  Annotations Get() override {
    ++calls_;
    if (is_missing_annotations_) {
      return {};
    }

    return {{"num_calls", std::to_string(calls_)}};
  }

  bool IsMissingAnnotations() const override { return is_missing_annotations_; }

 private:
  size_t calls_{0};
  bool is_missing_annotations_;
};

TEST(AnnotationManagerTest, ImmediatelyAvailable) {
  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  DynamicNonPlatform non_platform;

  {
    AnnotationManager mananger({"annotation1", "annotation2", "num_calls"}, static_annotations,
                               &non_platform);

    EXPECT_THAT(mananger.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                     MakePair("annotation1", "value1"),
                                                     MakePair("annotation2", Error::kMissingValue),
                                                     MakePair("num_calls", "1"),
                                                 }));
  }
}

TEST(AnnotationManagerTest, StaticAllowlist) {
  const Annotations static_annotations({
      {"annotation1", "value1"},
      {"annotation2", Error::kMissingValue},
  });

  DynamicNonPlatform counter;
  {
    AnnotationManager mananger({}, static_annotations, nullptr, {&counter});

    EXPECT_THAT(mananger.ImmediatelyAvailable(), IsEmpty());
  }

  {
    AnnotationManager mananger({"annotation1"}, static_annotations, nullptr, {&counter});

    EXPECT_THAT(mananger.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                     MakePair("annotation1", "value1"),
                                                 }));
  }

  {
    AnnotationManager mananger({"annotation1", "num_calls"}, static_annotations, nullptr,
                               {&counter});

    EXPECT_THAT(mananger.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                     MakePair("annotation1", "value1"),
                                                     MakePair("num_calls", "3"),
                                                 }));
  }
}

TEST(AnnotationManagerTest, IsNotMissingNonPlatform) {
  DynamicNonPlatform non_platform(kNotIsMissingNonPlatform);

  {
    AnnotationManager mananger({}, {}, &non_platform);
    EXPECT_THAT(mananger.ImmediatelyAvailable(), UnorderedElementsAreArray({
                                                     MakePair("num_calls", "1"),
                                                 }));
    EXPECT_FALSE(mananger.IsMissingNonPlatformAnnotations());
  }
}

TEST(AnnotationManagerTest, IsMissingNonPlatform) {
  DynamicNonPlatform non_platform(kIsMissingNonPlatform);

  {
    AnnotationManager mananger({}, {}, &non_platform);

    EXPECT_THAT(mananger.ImmediatelyAvailable(), IsEmpty());
    EXPECT_TRUE(mananger.IsMissingNonPlatformAnnotations());
  }
}

TEST(AnnotationManagerTest, UniqueKeys) {
  ASSERT_DEATH(
      {
        AnnotationManager mananger({"annotation1"});
        mananger.InsertStatic({{"annotation1", "value1"}});
        mananger.InsertStatic({{"annotation1", "value2"}});
      },
      "Attempting to re-insert annotation1");
}

}  // namespace
}  // namespace forensics::feedback
