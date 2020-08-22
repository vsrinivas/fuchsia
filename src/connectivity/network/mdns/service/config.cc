// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/config.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include "src/connectivity/network/mdns/service/mdns_names.h"
#include "src/lib/json_parser/rapidjson_validation.h"

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
    "v4_multicast_address": {
      "type": "string",
      "minLength": 7,
      "maxLength": 15
    },
    "v6_multicast_address": {
      "type": "string",
      "minLength": 4,
      "maxLength": 39
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
            "minLength": 8,
            "maxLength": 22
          },
          "instance": {
            "type": "string",
            "minLength": 1,
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
          },
          "media": {
            "type": "string",
            "enum": ["wired", "wireless", "both"]
          }
        },
        "required": ["service","port"]
      }
    }
  }
})";

const char kPortKey[] = "port";
const char kV4MultcastAddressKey[] = "v4_multicast_address";
const char kV6MultcastAddressKey[] = "v6_multicast_address";
const char kPerformHostNameProbeKey[] = "perform_host_name_probe";
const char kPublicationsKey[] = "publications";
const char kServiceKey[] = "service";
const char kInstanceKey[] = "instance";
const char kTextKey[] = "text";
const char kPerformProbeKey[] = "perform_probe";
const char kMediaKey[] = "media";
const char kMediaValueWired[] = "wired";
const char kMediaValueWireless[] = "wireless";
const char kMediaValueBoth[] = "both";

}  // namespace

//  static
const char Config::kConfigDir[] = "/config/data";

void Config::ReadConfigFiles(const std::string& host_name, const std::string& config_dir) {
  FX_DCHECK(MdnsNames::IsValidHostName(host_name));

  auto schema_result = json_parser::InitSchema(kSchema);
  FX_CHECK(schema_result.is_ok()) << schema_result.error_value().ToString();
  auto schema = std::move(schema_result.value());
  // |ParseFromDirectory| treats a non-existent directory the same as an empty directory, which
  // is what we want.
  parser_.ParseFromDirectory(config_dir, [this, &schema, &host_name](rapidjson::Document document) {
    auto validation_result = json_parser::ValidateSchema(document, schema);
    if (validation_result.is_error()) {
      parser_.ReportError(validation_result.error_value());
      return;
    }

    IntegrateDocument(document, host_name);
  });
}

void Config::IntegrateDocument(const rapidjson::Document& document, const std::string& host_name) {
  FX_DCHECK(document.IsObject());

  if (document.HasMember(kPortKey)) {
    FX_DCHECK(document[kPortKey].IsUint());
    FX_DCHECK(document[kPortKey].GetUint() >= 1);
    FX_DCHECK(document[kPortKey].GetUint() <= 65535);
    addresses_.SetPort(inet::IpPort::From_uint16_t(document[kPortKey].GetUint()));
  }

  if (document.HasMember(kV4MultcastAddressKey)) {
    FX_DCHECK(document[kV4MultcastAddressKey].IsString());
    auto address =
        inet::IpAddress::FromString(document[kV4MultcastAddressKey].GetString(), AF_INET);
    if (!address.is_valid()) {
      parser_.ReportError((std::stringstream() << kV4MultcastAddressKey << " value "
                                               << document[kV4MultcastAddressKey].GetString()
                                               << " is not a valid IPV4 address.")
                              .str());
      return;
    }

    addresses_.SetMulticastAddress(address);
  }

  if (document.HasMember(kV6MultcastAddressKey)) {
    FX_DCHECK(document[kV6MultcastAddressKey].IsString());
    auto address =
        inet::IpAddress::FromString(document[kV6MultcastAddressKey].GetString(), AF_INET6);
    if (!address.is_valid()) {
      parser_.ReportError((std::stringstream() << kV6MultcastAddressKey << " value "
                                               << document[kV6MultcastAddressKey].GetString()
                                               << " is not a valid IPV6 address.")
                              .str());
      return;
    }

    addresses_.SetMulticastAddress(address);
  }

  if (document.HasMember(kPerformHostNameProbeKey)) {
    FX_DCHECK(document[kPerformHostNameProbeKey].IsBool());
    SetPerformHostNameProbe(document[kPerformHostNameProbeKey].GetBool());
    if (parser_.HasError()) {
      return;
    }
  }

  if (document.HasMember(kPublicationsKey)) {
    FX_DCHECK(document[kPublicationsKey].IsArray());
    for (auto& item : document[kPublicationsKey].GetArray()) {
      IntegratePublication(item, host_name);
      if (parser_.HasError()) {
        return;
      }
    }
  }
}

void Config::IntegratePublication(const rapidjson::Value& value, const std::string& host_name) {
  FX_DCHECK(value.IsObject());
  FX_DCHECK(value.HasMember(kServiceKey));
  FX_DCHECK(value[kServiceKey].IsString());
  FX_DCHECK(value.HasMember(kPortKey));
  FX_DCHECK(value[kPortKey].IsUint());
  FX_DCHECK(value[kPortKey].GetUint() >= 1);
  FX_DCHECK(value[kPortKey].GetUint() <= 65535);

  auto service = value[kServiceKey].GetString();
  if (!MdnsNames::IsValidServiceName(service)) {
    parser_.ReportError((std::stringstream()
                         << kServiceKey << " value " << service << " is not a valid service name.")
                            .str());
    return;
  }

  std::string instance;
  if (value.HasMember(kInstanceKey)) {
    instance = value[kInstanceKey].GetString();
    if (!MdnsNames::IsValidInstanceName(instance)) {
      parser_.ReportError((std::stringstream() << kInstanceKey << " value " << instance
                                               << " is not a valid instance name.")
                              .str());
      return;
    }
  } else {
    instance = host_name;
    if (!MdnsNames::IsValidInstanceName(instance)) {
      parser_.ReportError((std::stringstream() << "Publication of service " << service
                                               << " specifies that the host name should be "
                                                  "used as the instance name, but "
                                               << host_name << "is not a valid instance name.")
                              .str());
      return;
    }
  }

  std::vector<std::string> text;
  if (value.HasMember(kTextKey)) {
    FX_DCHECK(value[kTextKey].IsArray());
    for (auto& item : value[kTextKey].GetArray()) {
      FX_DCHECK(item.IsString());
      if (!MdnsNames::IsValidTextString(item.GetString())) {
        parser_.ReportError((std::stringstream() << kTextKey << " item value " << item.GetString()
                                                 << " is not avalid text string.")
                                .str());
        return;
      }

      text.push_back(item.GetString());
    }
  }

  bool perform_probe = true;
  if (value.HasMember(kPerformProbeKey)) {
    FX_DCHECK(value[kPerformProbeKey].IsBool());
    perform_probe = value[kPerformProbeKey].GetBool();
  }

  Media media = Media::kBoth;
  if (value.HasMember(kMediaKey)) {
    FX_DCHECK(value[kMediaKey].IsString());
    std::string media_string = value[kMediaKey].GetString();
    if (media_string == kMediaValueWired) {
      media = Media::kWired;
    } else if (media_string == kMediaValueWireless) {
      media = Media::kWireless;
    } else {
      FX_DCHECK(media_string == kMediaValueBoth);
    }
  }

  publications_.emplace_back(Publication{
      .service_ = service,
      .instance_ = instance,
      .publication_ =
          Mdns::Publication::Create(inet::IpPort::From_uint16_t(value[kPortKey].GetUint()), text),
      .perform_probe_ = perform_probe,
      .media_ = media});
}

void Config::SetPerformHostNameProbe(bool perform_host_name_probe) {
  if (perform_host_name_probe_.has_value() &&
      perform_host_name_probe_.value() != perform_host_name_probe) {
    parser_.ReportError(
        (std::stringstream() << "Conflicting " << kPerformHostNameProbeKey << " value.").str());
    return;
  }

  perform_host_name_probe_ = perform_host_name_probe;
}

}  // namespace mdns
