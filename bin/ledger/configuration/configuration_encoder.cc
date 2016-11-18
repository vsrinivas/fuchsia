// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/configuration/configuration_encoder.h"

#include <string>

#include "apps/ledger/src/configuration/configuration.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace configuration {
namespace {
const char kSynchronization[] = "synchronization";
const char kFirebaseId[] = "firebase_id";
const char kFirebasePrefix[] = "firebase_prefix";
}

bool ConfigurationEncoder::Decode(const std::string& configuration_path,
                                  Configuration* configuration) {
  std::string json;
  if (!files::ReadFileToString(configuration_path.data(), &json)) {
    FTL_LOG(ERROR) << "Unable to read configuration in " << configuration_path;
    return false;
  }

  return DecodeFromString(json, configuration);
}

bool ConfigurationEncoder::Write(const std::string& configuration_path,
                                 const Configuration& configuration) {
  const std::string& data = EncodeToString(configuration);
  if (!files::WriteFile(configuration_path, data.c_str(), data.size())) {
    return false;
  }
  return true;
}

bool ConfigurationEncoder::DecodeFromString(const std::string& json,
                                            Configuration* configuration) {
  rapidjson::Document document;
  document.Parse(json.data(), json.size());

  if (document.HasParseError()) {
    return false;
  }

  if (!document.IsObject()) {
    return false;
  }

  Configuration new_configuration;

  if (!document.HasMember(kSynchronization)) {
    new_configuration.use_sync = false;
    *configuration = std::move(new_configuration);
    return true;
  }

  if (!document[kSynchronization].IsObject()) {
    FTL_LOG(ERROR) << "The " << kSynchronization
                   << " parameter must an object.";
    return false;
  }

  auto sync_config = document[kSynchronization].GetObject();
  new_configuration.use_sync = true;
  if (!sync_config.HasMember(kFirebaseId) ||
      !sync_config[kFirebaseId].IsString()) {
    FTL_LOG(ERROR) << "The " << kFirebaseId
                   << " parameter must be specified inside " << kSynchronization
                   << ".";
    return false;
  }

  new_configuration.sync_params.firebase_id = sync_config[kFirebaseId].GetString();

  if (!sync_config.HasMember(kFirebasePrefix) ||
      !sync_config[kFirebasePrefix].IsString()) {
    FTL_LOG(ERROR) << "The " << kFirebasePrefix
                   << " parameter must be specified inside " << kSynchronization
                   << ".";
    return false;
  }
  new_configuration.sync_params.firebase_prefix =
      sync_config[kFirebasePrefix].GetString();

  if (sync_config.MemberCount() != 2u) {
    FTL_LOG(ERROR) << "Unknown parameters specified inside " << kSynchronization
                   << ".";
    return false;
  }

  *configuration = std::move(new_configuration);
  return true;
}

std::string ConfigurationEncoder::EncodeToString(
    const Configuration& configuration) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();
  {
    if (!configuration.use_sync) {
      writer.EndObject();
      return string_buffer.GetString();
    }
    writer.Key(kSynchronization);

    writer.StartObject();
    {
      writer.Key(kFirebaseId);
      writer.String(configuration.sync_params.firebase_id.c_str(),
                    configuration.sync_params.firebase_id.size());
    }
    {
      writer.Key(kFirebasePrefix);
      writer.String(configuration.sync_params.firebase_prefix.c_str(),
                    configuration.sync_params.firebase_prefix.size());
    }
    writer.EndObject();
  }
  writer.EndObject();

  return string_buffer.GetString();
}

}  // namespace configuration
