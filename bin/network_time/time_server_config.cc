// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/network_time/time_server_config.h"

#include <string>
#include <vector>

#include <fstream>
#include <sstream>

#include <protocol.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>

#include "lib/syslog/cpp/logger.h"

#define MULTILINE(...) #__VA_ARGS__

namespace time_zone {

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

std::vector<RoughTimeServer> TimeServerConfig::ServerList() {
  return server_list_;
}

bool checkSchema(rapidjson::Document& d) {
  rapidjson::Document sd;
  if (sd.Parse(config_schema).HasParseError()) {
    FX_LOGS(ERROR) << "Schema not valid";
    return false;
  }
  rapidjson::SchemaDocument schema(sd);
  rapidjson::SchemaValidator validator(schema);
  if (!d.Accept(validator)) {
    // Input JSON is invalid according to the schema
    // Output diagnostic information
    rapidjson::StringBuffer sb;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
    FX_LOGS(ERROR) << "Invalid schema: " << sb.GetString();
    FX_LOGS(ERROR) << "Invalid keyword: "
                   << validator.GetInvalidSchemaKeyword();
    sb.Clear();
    validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
    FX_LOGS(ERROR) << "Invalid document: " << sb.GetString();
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
    FX_LOGS(ERROR) << "JSON parse error: "
                   << rapidjson::GetParseError_En(ok.Code()) << "("
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
      uint8_t public_key[ED25519_PUBLIC_KEY_LEN];
      if (public_key_str.length() != ED25519_PUBLIC_KEY_LEN * 2) {
        FX_LOGS(ERROR) << "Invalid public key: " << public_key_str;
        return false;
      }
      for (int k = 0; k < ED25519_PUBLIC_KEY_LEN; k++) {
        char hex[3] = {0};
        hex[0] = public_key_str.at(k * 2);
        hex[1] = public_key_str.at(k * 2 + 1);
        public_key[k] = strtoul(hex, NULL, 16);
      }

      RoughTimeServer server(std::move(name), std::move(address_str),
                             public_key, ED25519_PUBLIC_KEY_LEN);
      server_list_.push_back(server);
    }
  }
  return true;
}

}  // namespace time_zone
