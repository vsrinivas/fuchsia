// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/process_explorer/writer.h"

#include "third_party/rapidjson/include/rapidjson/document.h"
#include "third_party/rapidjson/include/rapidjson/rapidjson.h"
#include "third_party/rapidjson/include/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/include/rapidjson/writer.h"

namespace process_explorer {

std::string WriteProcessesDataAsJson(std::vector<Process> processes_data) {
  rapidjson::Document json_document;
  json_document.SetObject();
  auto& allocator = json_document.GetAllocator();

  rapidjson::Value processes_json(rapidjson::kArrayType);
  processes_json.Reserve((unsigned int)processes_data.size(), allocator);

  for (const auto& process : processes_data) {
    rapidjson::Value process_objects_json(rapidjson::kArrayType);
    process_objects_json.Reserve((unsigned int)process.objects.size(), allocator);

    for (const auto& object : process.objects) {
      rapidjson::Value object_json(rapidjson::kObjectType);
      object_json.AddMember("type", object.type, allocator)
          .AddMember("koid", object.koid, allocator)
          .AddMember("related_koid", object.related_koid, allocator)
          .AddMember("peer_owner_koid", object.peer_owner_koid, allocator);
      process_objects_json.PushBack(object_json, allocator);
    }

    rapidjson::Value process_name(rapidjson::kObjectType);
    const std::string s(process.name);
    process_name.SetString(s.c_str(), static_cast<rapidjson::SizeType>(s.length()), allocator);

    rapidjson::Value process_json(rapidjson::kObjectType);
    process_json.AddMember("koid", process.koid, allocator)
        .AddMember("name", process_name, allocator)
        .AddMember("objects", process_objects_json, allocator);
    processes_json.PushBack(process_json, allocator);
  }

  json_document.AddMember("Processes", processes_json, allocator);

  rapidjson::StringBuffer buffer;
  buffer.Clear();
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  json_document.Accept(writer);

  return std::string(buffer.GetString(), buffer.GetSize());
}

}  // namespace process_explorer
