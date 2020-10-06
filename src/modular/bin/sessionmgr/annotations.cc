// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/annotations.h"

namespace modular::annotations {

using Annotation = fuchsia::modular::Annotation;

std::vector<Annotation> Merge(std::vector<Annotation> a, std::vector<Annotation> b) {
  std::vector<Annotation> result;

  std::map<std::string, Annotation> b_by_key;
  std::transform(std::make_move_iterator(b.begin()), std::make_move_iterator(b.end()),
                 std::inserter(b_by_key, b_by_key.end()), [](Annotation&& annotation) {
                   return std::make_pair(annotation.key, std::move(annotation));
                 });

  for (auto it = std::make_move_iterator(a.begin()); it != std::make_move_iterator(a.end()); ++it) {
    // Omit annotations in `a` that have `null` values.
    if (!it->value) {
      continue;
    }

    // If `b` contains an annotation with the same key, use its value, unless it's null,
    // in which case it's omitted.
    if (b_by_key.count(it->key)) {
      if (b_by_key[it->key].value) {
        result.push_back(std::move(b_by_key.at(it->key)));
      }

      continue;
    }

    result.push_back(std::move(*it));
  }

  for (auto it = std::make_move_iterator(b_by_key.begin());
       it != std::make_move_iterator(b_by_key.end()); ++it) {
    // Omit annotations in `b` that have `null` values.
    // We have already omitted the ones from `a` that have the same key.
    if (!it->second.value) {
      continue;
    }

    result.push_back(std::move(it->second));
  }

  return result;
}

std::string ToInspect(const fuchsia::modular::AnnotationValue& value) {
  std::string text;
  switch (value.Which()) {
    case fuchsia::modular::AnnotationValue::Tag::kText:
      text = value.text();
      break;
    case fuchsia::modular::AnnotationValue::Tag::kBytes:
      text = "bytes";
      break;
    case fuchsia::modular::AnnotationValue::Tag::kBuffer:
      text = "buffer";
      break;
    case fuchsia::modular::AnnotationValue::Tag::kUnknown:
      text = "unknown";
      break;
    case fuchsia::modular::AnnotationValue::Tag::Invalid:
      text = "invalid";
      break;
  }
  return text;
}

fuchsia::session::Annotation ToSessionAnnotation(const fuchsia::modular::Annotation& annotation) {
  std::unique_ptr<fuchsia::session::Value> value;
  if (annotation.value->is_buffer()) {
    fuchsia::mem::Buffer buffer;
    annotation.value->buffer().Clone(&buffer);
    value = std::make_unique<fuchsia::session::Value>(
        fuchsia::session::Value::WithBuffer(std::move(buffer)));
  } else {
    value = std::make_unique<fuchsia::session::Value>(
        fuchsia::session::Value::WithText(std::string{annotation.value->text()}));
  }

  return fuchsia::session::Annotation{.key = std::string{annotation.key},
                                      .value = std::move(value)};
}

fuchsia::session::Annotations ToSessionAnnotations(
    const std::vector<fuchsia::modular::Annotation>& annotations) {
  std::vector<fuchsia::session::Annotation> custom_annotations;
  custom_annotations.reserve(annotations.size());

  for (const fuchsia::modular::Annotation& annotation : annotations) {
    custom_annotations.push_back(modular::annotations::ToSessionAnnotation(annotation));
  }

  fuchsia::session::Annotations session_annotations;
  session_annotations.set_custom_annotations(std::move(custom_annotations));

  return session_annotations;
}

}  // namespace modular::annotations

namespace session::annotations {

fuchsia::modular::Annotation ToModularAnnotation(const fuchsia::session::Annotation& annotation) {
  std::unique_ptr<fuchsia::modular::AnnotationValue> value;
  if (annotation.value->is_buffer()) {
    fuchsia::mem::Buffer buffer;
    annotation.value->buffer().Clone(&buffer);
    value = std::make_unique<fuchsia::modular::AnnotationValue>(
        fuchsia::modular::AnnotationValue::WithBuffer(std::move(buffer)));
  } else {
    value = std::make_unique<fuchsia::modular::AnnotationValue>(
        fuchsia::modular::AnnotationValue::WithText(std::string{annotation.value->text()}));
  }

  return fuchsia::modular::Annotation{.key = annotation.key, .value = std::move(value)};
}

std::vector<fuchsia::modular::Annotation> ToModularAnnotations(
    const fuchsia::session::Annotations& annotations) {
  if (!annotations.has_custom_annotations()) {
    return {};
  }

  std::vector<fuchsia::modular::Annotation> modular_annotations;
  for (const auto& annotation : annotations.custom_annotations()) {
    modular_annotations.push_back(ToModularAnnotation(annotation));
  }

  return modular_annotations;
}

}  // namespace session::annotations
