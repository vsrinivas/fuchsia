// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/encode.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/prettywriter.h"

namespace forensics::feedback {

template <>
fuchsia::feedback::Annotations Encode(const Annotations& annotations) {
  std::vector<fuchsia::feedback::Annotation> vec;
  for (const auto& [key, value] : annotations) {
    if (value.HasValue()) {
      fuchsia::feedback::Annotation annotation;
      annotation.key = key;
      annotation.value = value.Value();
      vec.push_back(std::move(annotation));
    }
  }

  fuchsia::feedback::Annotations result;
  if (!vec.empty()) {
    result.set_annotations(std::move(vec));
  }

  return result;
}

template <>
std::string Encode(const Annotations& annotations) {
  rapidjson::Document doc;
  doc.SetObject();

  auto& alloc = doc.GetAllocator();
  for (const auto& [key, value] : annotations) {
    if (value.HasValue()) {
      doc.AddMember(rapidjson::Value(key, alloc), rapidjson::Value(value.Value(), alloc), alloc);
    }
  }

  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  if (!writer.IsComplete()) {
    FX_LOGS(WARNING) << "Failed to write annotations as a JSON";
    return "";
  }

  return std::string(buffer.GetString(), buffer.GetSize());
}

}  // namespace forensics::feedback
