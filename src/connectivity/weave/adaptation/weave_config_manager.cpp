// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "weave_config_manager.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/lib/files/file.h"
#include "src/lib/json_parser/json_parser.h"
#include "third_party/modp_b64/modp_b64.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

namespace {
constexpr char kWeaveConfigStorePath[] = "/data/config.json";
}  // namespace

WeaveConfigManager& WeaveConfigManager::GetInstance() {
  static WeaveConfigManager manager;
  return manager;
}

WeaveConfigManager::WeaveConfigManager() : WeaveConfigManager(kWeaveConfigStorePath) {}

WeaveConfigManager::WeaveConfigManager(const std::string& path) {
  json::JSONParser json_parser_;
  if (files::IsFile(path)) {
    config_ = json_parser_.ParseFromFile(path);
  } else {
    config_.SetObject();
    FXL_CHECK(CommitKVPairs() == WEAVE_NO_ERROR) << "Failed to write init configuration to disk.";
  }
  FXL_CHECK(!json_parser_.HasError())
      << "Failed to load configuration: " << json_parser_.error_str();
}

WEAVE_ERROR WeaveConfigManager::ReadConfigValue(const std::string& key, bool* value) {
  rapidjson::Value config_value;
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  if ((error = ReadKVPair(key, config_value)) != WEAVE_NO_ERROR) {
    return error;
  } else if (!config_value.IsBool()) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }
  *value = config_value.GetBool();
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR WeaveConfigManager::ReadConfigValue(const std::string& key, uint32_t* value) {
  rapidjson::Value config_value;
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  if ((error = ReadKVPair(key, config_value)) != WEAVE_NO_ERROR) {
    return error;
  } else if (!config_value.IsUint()) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }
  *value = config_value.GetUint();
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR WeaveConfigManager::ReadConfigValue(const std::string& key, uint64_t* value) {
  rapidjson::Value config_value;
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  if ((error = ReadKVPair(key, config_value)) != WEAVE_NO_ERROR) {
    return error;
  } else if (!config_value.IsUint64()) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }
  *value = config_value.GetUint64();
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR WeaveConfigManager::ReadConfigValueStr(const std::string& key, char* value,
                                                   size_t value_size, size_t* out_size) {
  rapidjson::Value config_value;
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  if ((error = ReadKVPair(key, config_value)) != WEAVE_NO_ERROR) {
    return error;
  } else if (!config_value.IsString()) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }
  const std::string string_value(config_value.GetString());
  if (value_size < (string_value.size() + 1)) {
    return WEAVE_ERROR_BUFFER_TOO_SMALL;
  }
  *out_size = string_value.size() + 1;
  strncpy(value, string_value.c_str(), *out_size);
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR WeaveConfigManager::ReadConfigValueBin(const std::string& key, uint8_t* value,
                                                   size_t value_size, size_t* out_size) {
  rapidjson::Value config_value;
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  if ((error = ReadKVPair(key, config_value)) != WEAVE_NO_ERROR) {
    return error;
  } else if (!config_value.IsString()) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }
  std::string string_value(config_value.GetString());
  const std::string decoded_value(modp_b64_decode(string_value));
  if (value_size < decoded_value.size()) {
    return WEAVE_ERROR_BUFFER_TOO_SMALL;
  }
  *out_size = decoded_value.size();
  memcpy(value, decoded_value.c_str(), decoded_value.size());
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR WeaveConfigManager::WriteConfigValue(const std::string& key, bool value) {
  return WriteKVPair(key, rapidjson::Value(value).Move());
}

WEAVE_ERROR WeaveConfigManager::WriteConfigValue(const std::string& key, uint32_t value) {
  return WriteKVPair(key, rapidjson::Value(value).Move());
}

WEAVE_ERROR WeaveConfigManager::WriteConfigValue(const std::string& key, uint64_t value) {
  return WriteKVPair(key, rapidjson::Value(value).Move());
}

WEAVE_ERROR WeaveConfigManager::WriteConfigValueStr(const std::string& key, const char* value,
                                                    size_t value_size) {
  rapidjson::Value string_value;
  {
    const std::lock_guard<std::mutex> write_lock(config_mutex_);
    string_value.SetString(value, value_size, config_.GetAllocator());
  }
  return WriteKVPair(key, string_value.Move());
}

WEAVE_ERROR WeaveConfigManager::WriteConfigValueBin(const std::string& key, const uint8_t* value,
                                                    size_t value_size) {
  std::string binary_string((const char*)value, value_size);
  std::string encoded_string(modp_b64_encode(binary_string));
  return WriteConfigValueStr(key, encoded_string.c_str(), encoded_string.size());
}

WEAVE_ERROR WeaveConfigManager::ClearConfigValue(const std::string& key) {
  const std::lock_guard<std::mutex> write_lock(config_mutex_);
  config_.RemoveMember(key);
  return CommitKVPairs();
}

bool WeaveConfigManager::ConfigValueExists(const std::string& key) {
  rapidjson::Value value;
  return ReadKVPair(key, value) == WEAVE_NO_ERROR;
}

WEAVE_ERROR WeaveConfigManager::FactoryResetConfig() {
  const std::lock_guard<std::mutex> write_lock(config_mutex_);
  config_.RemoveAllMembers();
  return CommitKVPairs();
}

WEAVE_ERROR WeaveConfigManager::ReadKVPair(const std::string& key, rapidjson::Value& value) {
  const std::lock_guard<std::mutex> read_lock(config_mutex_);
  if (!config_.HasMember(key)) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }
  value.CopyFrom(config_[key], config_.GetAllocator());
  return value.IsNull() ? WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND : WEAVE_NO_ERROR;
}

WEAVE_ERROR WeaveConfigManager::WriteKVPair(const std::string& key, rapidjson::Value& value) {
  const std::lock_guard<std::mutex> write_lock(config_mutex_);
  rapidjson::Value key_value;
  key_value.SetString(key.c_str(), key.size(), config_.GetAllocator());
  config_.AddMember(key_value, value, config_.GetAllocator());
  return CommitKVPairs();
}

WEAVE_ERROR WeaveConfigManager::CommitKVPairs() {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  config_.Accept(writer);

  const std::string output(buffer.GetString());
  return files::WriteFile(kWeaveConfigStorePath, output.c_str(), output.size())
             ? WEAVE_NO_ERROR
             : WEAVE_ERROR_PERSISTED_STORAGE_FAIL;
}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
