// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/time/lib/network_time/time_server_config.h"

#include <lib/syslog/cpp/macros.h>
#include <protocol.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#define MULTILINE(...) #__VA_ARGS__

namespace time_server {

const char* config_schema = MULTILINE({
  "$schema" : "http://json-schema.org/draft-04/schema#",
  "properties" : {
    "servers" : {
      "items" : {
        "properties" : {
          "addresses" : {
            "items" : {
              "properties" : {"address" : {"type" : "string"}},
              "required" : ["address"],
              "type" : "object"
            },
            "minItems" : 1,
            "type" : "array"
          },
          "name" : {"type" : "string"},
          "publicKey" : {"maxLength" : 64, "minLength" : 64, "type" : "string"}
        },
        "required" : [ "publicKey", "addresses", "name" ],
        "type" : "object"
      },
      "minItems" : 1,
      "type" : "array"
    }
  },
  "required" : ["servers"],
  "type" : "object"
});

static bool readFile(std::string* out_contents, const char* filename) {
  std::ifstream serverFile;
  serverFile.open(filename);
  if ((serverFile.rdstate() & std::ifstream::failbit) != 0) {
    FX_LOGS(ERROR) << "Opening " << filename << ": " << strerror(errno);
    return false;
  }
  std::stringstream strStream;
  strStream << serverFile.rdbuf();
  out_contents->assign(strStream.str());
  return true;
}

std::vector<RoughTimeServer> TimeServerConfig::ServerList() { return server_list_; }

bool checkSchema(rapidjson::Document& d) {
  rapidjson::Document sd;
  if (sd.Parse(config_schema).HasParseError()) {
    FX_LOGS(WARNING) << "Schema not valid";
    return false;
  }
  rapidjson::SchemaDocument schema(sd);
  rapidjson::SchemaValidator validator(schema);
  if (!d.Accept(validator)) {
    // Input JSON is invalid according to the schema
    // Output diagnostic information
    rapidjson::StringBuffer sb;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
    FX_LOGS(WARNING) << "Invalid schema: " << sb.GetString();
    FX_LOGS(WARNING) << "Invalid keyword: " << validator.GetInvalidSchemaKeyword();
    sb.Clear();
    validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
    FX_LOGS(WARNING) << "Invalid document: " << sb.GetString();
    return false;
  }
  return true;
}

bool TimeServerConfig::Parse(std::string config_file) {
  std::string json;
  if (!readFile(&json, config_file.c_str())) {
    return false;
  }

  rapidjson::Document doc;
  rapidjson::ParseResult ok = doc.Parse(json.c_str());
  if (!ok) {
    FX_LOGS(WARNING) << "JSON parse error: " << rapidjson::GetParseError_En(ok.Code()) << "("
                     << ok.Offset() << ")";
    return false;
  }
  if (!checkSchema(doc)) {
    return false;
  }

  const rapidjson::Value& servers = doc["servers"];
  for (rapidjson::SizeType i = 0; i < servers.Size(); i++) {
    const rapidjson::Value& server = servers[i];

    const rapidjson::Value& addresses = server["addresses"];
    std::string name = server["name"].GetString();
    std::string public_key_str = server["publicKey"].GetString();
    for (rapidjson::SizeType j = 0; j < addresses.Size(); j++) {
      const rapidjson::Value& address = addresses[j];

      std::string address_str = address["address"].GetString();
      uint8_t public_key[roughtime::kPublicKeyLength];
      if (public_key_str.length() != roughtime::kPublicKeyLength * 2) {
        FX_LOGS(WARNING) << "Invalid public key: " << public_key_str;
        return false;
      }
      for (unsigned int k = 0; k < roughtime::kPublicKeyLength; k++) {
        char hex[3] = {0};
        hex[0] = public_key_str.at(k * 2);
        hex[1] = public_key_str.at(k * 2 + 1);
        public_key[k] = (uint8_t)strtoul(hex, NULL, 16);
      }

      RoughTimeServer server(name, std::move(address_str), public_key, roughtime::kPublicKeyLength);
      if (server.IsValid()) {
        server_list_.push_back(server);
      } else {
        FX_LOGS(ERROR) << "Roughtime configuration contained invalid server " << name;
      }
    }
  }
  return server_list_.size() > 0;
}

}  // namespace time_server
