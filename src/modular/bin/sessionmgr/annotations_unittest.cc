// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/annotations.h"

#include <lib/fidl/cpp/optional.h>

#include <memory>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

// TODO(fxbug.dev/37645): Return the proper properties instead of text strings.
TEST(AnnotationsTest, TextToInspect) {
  auto annotation_text = MakeAnnotation("string_key", "string_text");
  EXPECT_THAT(ToInspect(*annotation_text.value.get()), "string_text");
}

// TODO(fxbug.dev/37645): Return the proper properties instead of text strings.
TEST(AnnotationsTest, BufferToInspect) {
  fuchsia::mem::Buffer buffer{};
  std::string buffer_str = "x";
  ASSERT_TRUE(fsl::VmoFromString(buffer_str, &buffer));
  AnnotationValue annotation_value_buffer;
  annotation_value_buffer.set_buffer(std::move(buffer));
  auto annotation_buffer =
      Annotation{.key = std::move("buffer_key"),
                 .value = fidl::MakeOptional(std::move(annotation_value_buffer))};

  EXPECT_THAT(ToInspect(*annotation_buffer.value.get()), "buffer");
}

// TODO(fxbug.dev/37645): Return the proper properties instead of text strings.
TEST(AnnotationsTest, BytesToInspect) {
  AnnotationValue annotation_value_bytes;
  annotation_value_bytes.set_bytes({0x01, 0x02, 0x03, 0x04});
  auto annotation_bytes =
      Annotation{.key = std::move("bytes_key"),
                 .value = fidl::MakeOptional(std::move(annotation_value_bytes))};

  EXPECT_THAT(ToInspect(*annotation_bytes.value.get()), "bytes");
}

}  // namespace
}  // namespace modular::annotations

namespace session::annotations {
namespace {

using Annotation = fuchsia::session::Annotation;
using Value = fuchsia::session::Value;

using ::testing::ByRef;
using ::testing::UnorderedElementsAre;

TEST(SessionAnnotationsTest, ToModularAnnotationText) {
  static constexpr auto kTestAnnotationKey = "annotation_key";
  static constexpr auto kTestAnnotationValue = "annotation_value";

  auto annotation = Annotation{.key = kTestAnnotationKey,
                               .value = fidl::MakeOptional(Value::WithText(kTestAnnotationValue))};

  auto modular_annotation = fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value =
          fidl::MakeOptional(fuchsia::modular::AnnotationValue::WithText(kTestAnnotationValue))};

  EXPECT_THAT(ToModularAnnotation(annotation),
              modular::annotations::AnnotationEq(ByRef(modular_annotation)));
}

TEST(SessionAnnotationsTest, ToModularAnnotationBuffer) {
  static constexpr auto kTestAnnotationKey = "annotation_key";
  static constexpr auto kTestAnnotationValue = "annotation_value";

  fuchsia::mem::Buffer buffer{};
  ASSERT_TRUE(fsl::VmoFromString(kTestAnnotationValue, &buffer));

  auto annotation = Annotation{.key = kTestAnnotationKey,
                               .value = fidl::MakeOptional(Value::WithBuffer(std::move(buffer)))};

  // Set the buffer again because it was moved into |annotation.value|.
  ASSERT_TRUE(fsl::VmoFromString(kTestAnnotationValue, &buffer));

  auto modular_annotation = fuchsia::modular::Annotation{
      .key = kTestAnnotationKey,
      .value =
          fidl::MakeOptional(fuchsia::modular::AnnotationValue::WithBuffer(std::move(buffer)))};

  EXPECT_THAT(ToModularAnnotation(annotation),
              modular::annotations::AnnotationEq(ByRef(modular_annotation)));
}

TEST(SessionAnnotationsTest, ToModularAnnotationsEmptyTable) {
  fuchsia::session::Annotations annotations;

  auto modular_annotations = ToModularAnnotations(annotations);

  EXPECT_TRUE(modular_annotations.empty());
}

TEST(SessionAnnotationsTest, ToModularAnnotationsEmptyCustomAnnotations) {
  fuchsia::session::Annotations annotations;
  annotations.set_custom_annotations({});

  auto modular_annotations = ToModularAnnotations(annotations);

  EXPECT_TRUE(modular_annotations.empty());
}

TEST(SessionAnnotationsTest, ToModularAnnotationsWithCustomAnnotations) {
  static constexpr auto kTestTextAnnotationKey = "text_annotation_key";
  static constexpr auto kTestTextAnnotationValue = "text_annotation_value";
  static constexpr auto kTestBufferAnnotationKey = "buffer_annotation_key";
  static constexpr auto kTestBufferAnnotationValue = "buffer_annotation_value";

  fuchsia::mem::Buffer buffer{};
  ASSERT_TRUE(fsl::VmoFromString(kTestBufferAnnotationValue, &buffer));

  auto text_annotation =
      Annotation{.key = kTestTextAnnotationKey,
                 .value = fidl::MakeOptional(Value::WithText(kTestTextAnnotationValue))};
  auto buffer_annotation =
      Annotation{.key = kTestBufferAnnotationKey,
                 .value = fidl::MakeOptional(Value::WithBuffer(std::move(buffer)))};

  fuchsia::session::Annotations annotations;
  annotations.mutable_custom_annotations()->push_back(std::move(text_annotation));
  annotations.mutable_custom_annotations()->push_back(std::move(buffer_annotation));

  auto modular_annotations = ToModularAnnotations(annotations);

  auto expected_text_annotation = fuchsia::modular::Annotation{
      .key = kTestTextAnnotationKey,
      .value = fidl::MakeOptional(
          fuchsia::modular::AnnotationValue::WithText(kTestTextAnnotationValue))};

  // Set the buffer again because it was moved into |buffer_annotation.value|.
  ASSERT_TRUE(fsl::VmoFromString(kTestBufferAnnotationValue, &buffer));

  auto expected_buffer_annotation = fuchsia::modular::Annotation{
      .key = kTestBufferAnnotationKey,
      .value =
          fidl::MakeOptional(fuchsia::modular::AnnotationValue::WithBuffer(std::move(buffer)))};

  EXPECT_THAT(
      modular_annotations,
      UnorderedElementsAre(modular::annotations::AnnotationEq(ByRef(expected_text_annotation)),
                           modular::annotations::AnnotationEq(ByRef(expected_buffer_annotation))));
}

}  // namespace
}  // namespace session::annotations
