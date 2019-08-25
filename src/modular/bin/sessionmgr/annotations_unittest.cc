// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/annotations.h"

#include <lib/fidl/cpp/optional.h>

#include <memory>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/modular/bin/sessionmgr/testing/annotations_matchers.h"

namespace modular::annotations {
namespace {

using Annotation = fuchsia::modular::Annotation;
using AnnotationValue = fuchsia::modular::AnnotationValue;

using ::testing::ByRef;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

Annotation MakeAnnotation(std::string key, std::string value) {
  AnnotationValue annotation_value;
  annotation_value.set_text(std::move(value));
  return Annotation{.key = std::move(key),
                    .value = fidl::MakeOptional(std::move(annotation_value))};
}

// Merging two empty vectors of annotations produces an empty vector.
TEST(AnnotationsTest, MergeEmpty) {
  std::vector<Annotation> a{};
  std::vector<Annotation> b{};
  EXPECT_THAT(Merge(std::move(a), std::move(b)), IsEmpty());
}

// Merging an empty vectors of annotations into a non-empty one produces the latter, unchanged.
TEST(AnnotationsTest, MergeEmptyIntoNonEmpty) {
  auto annotation = MakeAnnotation("foo", "bar");

  std::vector<Annotation> a{};
  a.push_back(fidl::Clone(annotation));
  std::vector<Annotation> b{};

  EXPECT_THAT(Merge(std::move(a), std::move(b)), ElementsAre(AnnotationEq(ByRef(annotation))));
}

// Merging an annotation with the same key, with a non-null value, overwrites the value.
TEST(AnnotationsTest, MergeOverwrite) {
  auto annotation_1 = MakeAnnotation("foo", "bar");
  auto annotation_2 = MakeAnnotation("foo", "quux");

  std::vector<Annotation> a{};
  a.push_back(fidl::Clone(annotation_1));
  std::vector<Annotation> b{};
  b.push_back(fidl::Clone(annotation_2));

  EXPECT_THAT(Merge(std::move(a), std::move(b)), ElementsAre(AnnotationEq(ByRef(annotation_2))));
}

// Merging an annotation with the same key, with a null value, removes the annotation.
TEST(AnnotationsTest, MergeNullValueDeletesExisting) {
  auto annotation_1 = MakeAnnotation("foo", "bar");
  auto annotation_2 = Annotation{.key = "foo"};

  std::vector<Annotation> a{};
  a.push_back(fidl::Clone(annotation_1));
  std::vector<Annotation> b{};
  b.push_back(fidl::Clone(annotation_2));

  EXPECT_THAT(Merge(std::move(a), std::move(b)), IsEmpty());
}

// Merging disjoint sets of annotations produces a union.
TEST(AnnotationsTest, MergeDisjoint) {
  auto annotation_1 = MakeAnnotation("foo", "bar");
  auto annotation_2 = MakeAnnotation("hello", "world");

  std::vector<Annotation> a{};
  a.push_back(fidl::Clone(annotation_1));
  std::vector<Annotation> b{};
  a.push_back(fidl::Clone(annotation_2));

  EXPECT_THAT(
      Merge(std::move(a), std::move(b)),
      UnorderedElementsAre(AnnotationEq(ByRef(annotation_1)), AnnotationEq(ByRef(annotation_2))));
}

}  // namespace
}  // namespace modular::annotations
