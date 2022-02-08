// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/utils.h"

#include <algorithm>

#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"

namespace forensics {
namespace feedback_data {

AnnotationKeys RestrictAllowlist(const AnnotationKeys& allowlist,
                                 const AnnotationKeys& restrict_to) {
  AnnotationKeys filtered;
  std::set_intersection(allowlist.begin(), allowlist.end(), restrict_to.begin(), restrict_to.end(),
                        std::inserter(filtered, filtered.begin()));
  return filtered;
}

Annotations ExtractAllowlisted(const AnnotationKeys& allowlist, const Annotations& annotations) {
  Annotations out;
  for (const auto& [k, v] : annotations) {
    if (allowlist.find(k) != allowlist.end()) {
      out.insert({k, v});
    }
  }

  return out;
}

Annotations WithError(const AnnotationKeys& allowlist, const Error error) {
  Annotations out;
  for (const auto& k : allowlist) {
    out.insert({k, error});
  }
  return out;
}

std::vector<fuchsia::feedback::Annotation> ToFeedbackAnnotationVector(
    const Annotations& annotations) {
  std::vector<fuchsia::feedback::Annotation> vec;
  for (const auto& [key, value] : annotations) {
    if (value.HasValue()) {
      fuchsia::feedback::Annotation annotation;
      annotation.key = key;
      annotation.value = value.Value();
      vec.push_back(std::move(annotation));
    }
  }
  return vec;
}

std::optional<std::string> ToJsonString(
    const std::vector<fuchsia::feedback::Annotation>& annotations) {
  rapidjson::Document json;
  json.SetObject();
  for (const auto& annotation : annotations) {
    json.AddMember(rapidjson::StringRef(annotation.key), annotation.value, json.GetAllocator());
  }
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  json.Accept(writer);
  if (!writer.IsComplete()) {
    FX_LOGS(WARNING) << "Failed to write annotations as a JSON";
    return std::nullopt;
  }
  return std::string(buffer.GetString(), buffer.GetSize());
}

}  // namespace feedback_data
}  // namespace forensics
