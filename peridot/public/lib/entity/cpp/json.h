// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ENTITY_CPP_JSON_H_
#define LIB_ENTITY_CPP_JSON_H_

#include <string>
#include <vector>

#include "rapidjson/document.h"

namespace modular {

// Returns a JSON string that can be used with EntityReferenceFromJson().
std::string EntityReferenceToJson(const std::string& ref);
// Like above but returns a rapidjson::Document.
rapidjson::Document EntityReferenceToJsonDoc(const std::string& ref);

// Returns false if |json| does not represent an fuchsia::modular::Entity
// reference.
bool EntityReferenceFromJson(const std::string& json, std::string* ref);
// Like above but operates on a rapidjson::Value.
bool EntityReferenceFromJson(const rapidjson::Value& value, std::string* ref);

// Extracts the values of the JSON object |doc|'s "@type" attribute into
// |types|. Returns false and leaves |types| untouched if |doc| is not JSON
// or is not structured in a way that we can extract types.
bool ExtractEntityTypesFromJson(const std::string& json,
                                std::vector<std::string>* output);
bool ExtractEntityTypesFromJson(const rapidjson::Value& value,
                                std::vector<std::string>* output);

}  // namespace modular

#endif  // LIB_ENTITY_CPP_JSON_H_
