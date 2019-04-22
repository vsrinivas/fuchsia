// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/config.h"

#include <sstream>

#include "garnet/bin/mdns/service/mdns_names.h"
#include "garnet/public/lib/rapidjson_utils/rapidjson_validation.h"
#include "src/lib/fxl/logging.h"

namespace mdns {
namespace {

const char kSchema[] = R"({
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "port": {
      "type": "integer",
      "minimum": 1,
      "maximum": 65535
    },
    "perform_host_name_probe": {
      "type": "boolean"
    },
    "publications": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "service": {
            "type": "string",
            "maxLength": 22
          },
          "instance": {
            "type": "string",
            "maxLength": 63
          },
          "port": {
            "type": "integer",
            "minimum": 1,
            "maximum": 65535
          },
          "text": {
            "type": "array",
            "items": {
              "type": "string",
              "maxLength": 255
            }
          },
          "perform_probe": {
            "type": "boolean"
          }
        },
        "required": ["service","port"]
      }
    }
  }
})";

const char kPerformHostNameProbeKey[] = "perform_host_name_probe";
const char kPublicationsKey[] = "publications";
const char kServiceKey[] = "service";
const char kInstanceKey[] = "instance";
const char kPortKey[] = "port";
const char kTextKey[] = "text";
const char kPerformProbeKey[] = "perform_probe";

}  // namespace

//  static
const char Config::kConfigDir[] = "/config/data";

void Config::ReadConfigFiles(const std::string& host_name,
                             const std::string& config_dir) {
  FXL_DCHECK(MdnsNames::IsValidHostName(host_name));

  auto schema = rapidjson_utils::InitSchema(kSchema);
  parser_.ParseFromDirectory(
      config_dir, [this, &schema, &host_name](rapidjson::Document document) {
        if (!rapidjson_utils::ValidateSchema(document, *schema)) {
          parser_.ReportError("Schema validation failure.");
          return;
        }

        IntegrateDocument(document, host_name);
      });
}

void Config::IntegrateDocument(const rapidjson::Document& document,
                               const std::string& host_name) {
  FXL_DCHECK(document.IsObject());

  if (document.HasMember(kPortKey)) {
    FXL_DCHECK(document[kPortKey].IsUint());
    FXL_DCHECK(document[kPortKey].GetUint() >= 1);
    FXL_DCHECK(document[kPortKey].GetUint() <= 65535);
    mdns_port_ = inet::IpPort::From_uint16_t(document[kPortKey].GetUint());
  }

  if (document.HasMember(kPerformHostNameProbeKey)) {
    FXL_DCHECK(document[kPerformHostNameProbeKey].IsBool());
    SetPerformHostNameProbe(document[kPerformHostNameProbeKey].GetBool());
    if (parser_.HasError()) {
      return;
    }
  }

  if (document.HasMember(kPublicationsKey)) {
    FXL_DCHECK(document[kPublicationsKey].IsArray());
    for (auto& item : document[kPublicationsKey].GetArray()) {
      IntegratePublication(item, host_name);
      if (parser_.HasError()) {
        return;
      }
    }
  }
}

void Config::IntegratePublication(const rapidjson::Value& value,
                                  const std::string& host_name) {
  FXL_DCHECK(value.IsObject());
  FXL_DCHECK(value.HasMember(kServiceKey));
  FXL_DCHECK(value[kServiceKey].IsString());
  FXL_DCHECK(value.HasMember(kPortKey));
  FXL_DCHECK(value[kPortKey].IsUint());
  FXL_DCHECK(value[kPortKey].GetUint() >= 1);
  FXL_DCHECK(value[kPortKey].GetUint() <= 65535);

  auto service = value[kServiceKey].GetString();
  if (!MdnsNames::IsValidServiceName(service)) {
    parser_.ReportError((std::stringstream()
                         << kServiceKey << " value " << service
                         << " is not a valid service name.")
                            .str());
    return;
  }

  std::string instance;
  if (value.HasMember(kInstanceKey)) {
    instance = value[kInstanceKey].GetString();
    if (!MdnsNames::IsValidInstanceName(instance)) {
      parser_.ReportError((std::stringstream()
                           << kInstanceKey << " value " << instance
                           << " is not a valid instance name.")
                              .str());
      return;
    }
  } else {
    instance = host_name;
    if (!MdnsNames::IsValidInstanceName(instance)) {
      parser_.ReportError((std::stringstream()
                           << "Publication of service " << service
                           << " specifies that the host name should be "
                              "used as the instance name, but "
                           << host_name << "is not a valid instance name.")
                              .str());
      return;
    }
  }

  std::vector<std::string> text;
  if (value.HasMember(kTextKey)) {
    FXL_DCHECK(value[kTextKey].IsArray());
    for (auto& item : value[kTextKey].GetArray()) {
      FXL_DCHECK(item.IsString());
      if (!MdnsNames::IsValidTextString(item.GetString())) {
        parser_.ReportError((std::stringstream()
                             << kTextKey << " item value " << item.GetString()
                             << " is not avalid text string.")
                                .str());
        return;
      }

      text.push_back(item.GetString());
    }
  }

  bool perform_probe = true;
  if (value.HasMember(kPerformProbeKey)) {
    FXL_DCHECK(value[kPerformProbeKey].IsBool());
    perform_probe = value[kPerformProbeKey].GetBool();
  }

  publications_.emplace_back(Publication{
      .service_ = service,
      .instance_ = instance,
      .publication_ = Mdns::Publication::Create(
          inet::IpPort::From_uint16_t(value[kPortKey].GetUint()), text),
      .perform_probe_ = perform_probe});
}

void Config::SetPerformHostNameProbe(bool perform_host_name_probe) {
  if (perform_host_name_probe_.has_value() &&
      perform_host_name_probe_.value() != perform_host_name_probe) {
    parser_.ReportError((std::stringstream()
                         << "Conflicting " << kPerformHostNameProbeKey
                         << " value.")
                            .str());
    return;
  }

  perform_host_name_probe_ = perform_host_name_probe;
}

}  // namespace mdns
