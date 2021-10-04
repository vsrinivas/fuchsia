// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/address_descriptor.h"

#include <sstream>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace storage::volume_image {
namespace {

rapidjson::Value ToValue(AddressMap map, rapidjson::Document::AllocatorType* allocator) {
  rapidjson::Value value = {};
  value.SetObject();
  value.AddMember("source", map.source, *allocator);
  value.AddMember("target", map.target, *allocator);
  value.AddMember("count", map.count, *allocator);
  if (map.size.has_value()) {
    value.AddMember("size", map.size.value(), *allocator);
  }

  if (!map.options.empty()) {
    rapidjson::Value option_map;
    option_map.SetObject();
    for (const auto& [key, value] : map.options) {
      rapidjson::Value value_key(key, *allocator);
      option_map.AddMember(value_key, value, *allocator);
    }
    value.AddMember("options", option_map, *allocator);
  }
  return value;
}

AddressMap FromValue(const rapidjson::Value& value) {
  AddressMap map = {};
  map.source = value["source"].GetUint64();
  map.target = value["target"].GetUint64();
  map.count = value["count"].GetUint64();
  map.size = std::nullopt;

  if (value.HasMember("size") && value["size"].IsUint64()) {
    map.size = value["size"].GetUint64();
  }

  if (value.HasMember("options") && value["options"].IsObject()) {
    const auto& option_map = value["options"].GetObject();
    for (const auto& option : option_map) {
      map.options[option.name.GetString()] = option.value.GetUint64();
    }
  }
  return map;
}

}  // namespace

fpromise::result<AddressDescriptor, std::string> AddressDescriptor::Deserialize(
    cpp20::span<const uint8_t> serialized) {
  rapidjson::Document document;
  rapidjson::ParseResult result =
      document.Parse(reinterpret_cast<const char*>(serialized.data()), serialized.size());

  if (result.IsError()) {
    std::ostringstream error;
    error << "Error parsing serialized AddressDescriptor. "
          << rapidjson::GetParseError_En(result.Code()) << std::endl;
    return fpromise::error(error.str());
  }

  uint64_t magic = document["magic"].GetUint64();
  if (magic != kMagic) {
    return fpromise::error("Invalid Magic\n");
  }

  if (!document.HasMember("mappings") || !document["mappings"].IsArray() ||
      document["mappings"].GetArray().Empty()) {
    return fpromise::error("AddressDescriptor must contain a non empty array field 'mapping'.\n");
  }

  AddressDescriptor descriptor = {};
  const auto& mapping_array = document["mappings"].GetArray();
  for (auto& mapping : mapping_array) {
    descriptor.mappings.push_back(FromValue(mapping));
  }

  return fpromise::ok(descriptor);
}

fpromise::result<std::vector<uint8_t>, std::string> AddressDescriptor::Serialize() const {
  rapidjson::Document document;
  document.SetObject();

  document.AddMember("magic", kMagic, document.GetAllocator());

  rapidjson::Value value_mappings;
  value_mappings.SetArray();
  for (auto mapping : mappings) {
    value_mappings.PushBack(ToValue(mapping, &document.GetAllocator()), document.GetAllocator());
  }
  document.AddMember("mappings", value_mappings, document.GetAllocator());

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  if (!document.Accept(writer)) {
    return fpromise::error("Failed to obtain string representation of AddressDescriptor.\n");
  }
  const auto* serialized_content = reinterpret_cast<const uint8_t*>(buffer.GetString());
  std::vector<uint8_t> data(serialized_content, serialized_content + buffer.GetLength());
  data.push_back('\0');

  return fpromise::ok(data);
}

std::string AddressMap::DebugString() const {
  std::string debug_string =
      "\n{\n"
      "   source: " +
      std::to_string(source) + "\n" + "   target: " + std::to_string(target) + "\n" +
      "   count:  " + std::to_string(count) + "\n" +
      "   size:   " + (size.has_value() ? std::to_string(size.value()) : "std::nullopt") + "\n" +
      "   options: {\n";
  for (const auto& option : options) {
    debug_string += "        " + option.first + ": " + std::to_string(option.second) + "\n";
  }
  debug_string += "   }\n";
  debug_string += "}\n";

  return debug_string;
}

}  // namespace storage::volume_image
