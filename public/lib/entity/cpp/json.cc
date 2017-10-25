// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/entity/cpp/json.h"

#include <string>

#include "garnet/public/lib/fxl/logging.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace modular {

constexpr char kEntityTypeProperty[] = "@type";
constexpr char kEntityRefAttribute[] = "@entityRef";

std::string EntityReferenceToJson(const std::string& ref) {
  auto doc = EntityReferenceToJsonDoc(ref);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);
  return buffer.GetString();
}
rapidjson::Document EntityReferenceToJsonDoc(const std::string& ref) {
  // Create an object that looks like:
  // {
  //   "@entityRef": "<ref string>"
  // }
  rapidjson::Document doc;
  doc.SetObject();
  rapidjson::Value str;
  str.SetString(ref, doc.GetAllocator());
  doc.AddMember(kEntityRefAttribute, str, doc.GetAllocator());
  return doc;
}

bool EntityReferenceFromJson(const std::string& json, std::string* ref) {
  rapidjson::Document doc;
  doc.Parse(json);
  if (doc.HasParseError())
    return false;
  return EntityReferenceFromJson(doc, ref);
}
bool EntityReferenceFromJson(const rapidjson::Value& value, std::string* ref) {
  if (!value.HasMember(kEntityRefAttribute))
    return false;

  *ref = value[kEntityRefAttribute].GetString();
  return true;
}

bool ExtractEntityTypesFromJson(const std::string& json,
                                std::vector<std::string>* const output) {
  FXL_CHECK(output != nullptr);
  // If the content has the @type attribute, take its contents and populate the
  // EntityMetadata appropriately, overriding whatever is there.
  rapidjson::Document doc;
  doc.Parse(json);
  if (doc.HasParseError()) {
    return false;
  }

  std::vector<std::string> entity_types;
  if (doc.IsObject() && doc.HasMember(kEntityTypeProperty)) {
    const auto& types = doc[kEntityTypeProperty];
    if (types.IsString()) {
      entity_types.push_back(types.GetString());
    } else if (types.IsArray()) {
      for (uint32_t i = 0; i < types.Size(); ++i) {
        if (!types[i].IsString())
          return false;
        entity_types.push_back(types[i].GetString());
      }
    } else {
      return false;
    }
  }

  output->swap(entity_types);
  return true;
}

}  // namespace modular
