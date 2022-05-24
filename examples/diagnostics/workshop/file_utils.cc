// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <rapidjson/document.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/json_parser/json_parser.h"

std::string FilepathForKey(std::string& key) { return files::JoinPath("/data", key); }

bool LoadFromFile(std::string& filepath, std::string* name, int64_t* balance) {
  json::JSONParser json_parser;
  rapidjson::Document document = json_parser.ParseFromFile(filepath);
  if (json_parser.HasError()) {
    return false;
  }
  *name = document["name"].GetString();
  *balance = document["balance"].GetInt();
  return true;
}

bool SaveToFile(std::string& filepath, std::string& name, int64_t balance) {
  rapidjson::Document document;
  document.SetObject();
  document.AddMember("name", name, document.GetAllocator());
  document.AddMember("balance", balance, document.GetAllocator());
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  return files::WriteFile(filepath, buffer.GetString());
}
