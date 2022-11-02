// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/config.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include "src/connectivity/network/mdns/service/common/mdns_names.h"
#include "src/connectivity/network/mdns/service/common/type_converters.h"
#include "src/lib/json_parser/rapidjson_validation.h"

namespace mdns {
namespace {

const char kSchema[] = R"({
  "type": "object",
  "additionalProperties": false,
  "properties": {
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
    },
    "alt_services": {
      "type": "array",
      "items": {
        "type": "string",
        "minLength": 8,
        "maxLength": 22
      }
    }
  }
})";

const char kPortKey[] = "port";
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

// TODO(fxb/113901): Remove this when alt_services is no longer needed.
const char kAltServicesKey[] = "alt_services";

}  // namespace

//  static
const char Config::kConfigDir[] = "/config/data";

void Config::ReadConfigFiles(const std::string& local_host_name, const std::string& config_dir) {
  FX_DCHECK(MdnsNames::IsValidHostName(local_host_name));

  auto schema_result = json_parser::InitSchema(kSchema);
  FX_CHECK(schema_result.is_ok()) << schema_result.error_value().ToString();
  auto schema = std::move(schema_result.value());
  // |ParseFromDirectory| treats a non-existent directory the same as an empty directory, which
  // is what we want.
  parser_.ParseFromDirectory(
      config_dir, [this, &schema, &local_host_name](rapidjson::Document document) {
        auto validation_result = json_parser::ValidateSchema(document, schema);
        if (validation_result.is_error()) {
          parser_.ReportError(validation_result.error_value());
          return;
        }

        IntegrateDocument(document, local_host_name);
      });
}

void Config::IntegrateDocument(const rapidjson::Document& document,
                               const std::string& local_host_name) {
  FX_DCHECK(document.IsObject());

  if (document.HasMember(kPerformHostNameProbeKey)) {
    FX_DCHECK(document[kPerformHostNameProbeKey].IsBool());
    SetPerformHostNameProbe(document[kPerformHostNameProbeKey].GetBool());
    if (parser_.HasError()) {
      return;
    }
  }

  if (document.HasMember(kPublicationsKey)) {
    FX_DCHECK(document[kPublicationsKey].IsArray());
    for (const auto& item : document[kPublicationsKey].GetArray()) {
      IntegratePublication(item, local_host_name);
      if (parser_.HasError()) {
        return;
      }
    }
  }

  if (document.HasMember(kAltServicesKey)) {
    FX_DCHECK(document[kAltServicesKey].IsArray());
    for (const auto& item : document[kAltServicesKey].GetArray()) {
      FX_DCHECK(item.IsString());
      if (!MdnsNames::IsValidServiceName(item.GetString())) {
        parser_.ReportError((std::stringstream()
                             << kAltServicesKey << " item value " << item.GetString()
                             << " is not a valid service type.")
                                .str());
        return;
      }

      alt_services_.push_back(item.GetString());
    }
  }
}

void Config::IntegratePublication(const rapidjson::Value& value,
                                  const std::string& local_host_name) {
  FX_DCHECK(value.IsObject());
  FX_DCHECK(value.HasMember(kServiceKey));
  FX_DCHECK(value[kServiceKey].IsString());
  FX_DCHECK(value.HasMember(kPortKey));
  FX_DCHECK(value[kPortKey].IsUint());
  const unsigned int port = value[kPortKey].GetUint();
  FX_DCHECK(port >= 1);
  FX_DCHECK(port <= std::numeric_limits<uint16_t>::max()) << port << " doesn't fit in a uint16";

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
    instance = local_host_name;
    if (!MdnsNames::IsValidInstanceName(instance)) {
      parser_.ReportError((std::stringstream()
                           << "Publication of service " << service
                           << " specifies that the host name should be "
                              "used as the instance name, but "
                           << local_host_name << "is not a valid instance name.")
                              .str());
      return;
    }
  }

  std::vector<std::string> text;
  if (value.HasMember(kTextKey)) {
    FX_DCHECK(value[kTextKey].IsArray());
    for (const auto& item : value[kTextKey].GetArray()) {
      FX_DCHECK(item.IsString());
      if (!MdnsNames::IsValidTextString(item.GetString())) {
        parser_.ReportError((std::stringstream() << kTextKey << " item value " << item.GetString()
                                                 << " is not a valid text string.")
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
          Mdns::Publication::Create(inet::IpPort::From_uint16_t(static_cast<uint16_t>(port)),
                                    fidl::To<std::vector<std::vector<uint8_t>>>(text)),
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
