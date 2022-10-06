// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread_config_manager.h"

#include <sstream>
#include <string>

#include "openthread-system.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/lib/files/file.h"
#include "src/lib/json_parser/json_parser.h"
#include "third_party/modp_b64/modp_b64.h"

ThreadConfigManager::ThreadConfigManager(const std::string& path) : config_store_path_(path) {
  json::JSONParser json_parser_;
  if (files::IsFile(config_store_path_)) {
    std::vector<uint8_t> file_contents;
    if (files::ReadFileToVector(config_store_path_, &file_contents)) {
      otPlatLog(OT_LOG_LEVEL_DEBG, OT_LOG_REGION_PLATFORM, "Read the file: %s",
                config_store_path_.c_str());
      {
        std::stringstream file_contents_combined;
        std::copy(file_contents.begin(), file_contents.end(),
                  std::ostream_iterator<char>(file_contents_combined, ""));
        otPlatLog(OT_LOG_LEVEL_DEBG, OT_LOG_REGION_PLATFORM, "               ...with contents: %s",
                  file_contents_combined.str().c_str());
      }

    } else {
      otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM, "Failed to read file: %s to vector.",
                config_store_path_.c_str());
    }

    config_ = json_parser_.ParseFromFile(config_store_path_);
  } else {
    otPlatLog(OT_LOG_LEVEL_NOTE, OT_LOG_REGION_PLATFORM, "File: %s not present, will create new.",
              config_store_path_.c_str());
    config_.SetObject();
  }

  if (json_parser_.HasError()) {
    // Report error but don't crash if the file is not a valid json format.
    // Start from a blank object in that case.
    otPlatLog(OT_LOG_LEVEL_WARN, otLogRegion::OT_LOG_REGION_PLATFORM,
              "Failed to load configuration from file: %s with error: %s",
              config_store_path_.c_str(), json_parser_.error_str().c_str());
    otPlatLog(OT_LOG_LEVEL_WARN, otLogRegion::OT_LOG_REGION_PLATFORM,
              "Will assume no existing configuration.");

    config_.SetObject();
  }

  CommitKVPairs();
}

ThreadConfigManager::~ThreadConfigManager() {
  CommitKVPairs();
  otPlatLog(OT_LOG_LEVEL_DEBG, otLogRegion::OT_LOG_REGION_PLATFORM,
            "ThreadConfigManager being destroyed");
}

ThreadConfigMgrError ThreadConfigManager::AppendConfigValueBinArray(const std::string& key,
                                                                    const uint8_t* value,
                                                                    size_t value_size) {
  // Create a json value from the bin data:
  // - First create string from bin data:
  std::string binary_string((const char*)value, value_size);
  std::string encoded_string(modp_b64_encode(binary_string));
  // - Now create the json string from encoded string
  rapidjson::Value string_value;
  {
    const std::lock_guard<std::mutex> write_lock(config_mutex_);
    string_value.SetString(encoded_string.c_str(), encoded_string.size(), config_.GetAllocator());

    // If array exists, simply append to it
    rapidjson::Value::MemberIterator it = config_.FindMember(key);
    if (it != config_.MemberEnd()) {
      if (!(it->value).IsArray()) {
        // A key exists, but it's value isn't of type 'array'
        return kThreadConfigMgrErrorConflictingTypes;
      }
      (it->value).PushBack(string_value, config_.GetAllocator());
      return CommitKVPairs();
    }
  }

  // Create an array with the value
  rapidjson::Value array(rapidjson::kArrayType);
  array.PushBack(string_value, config_.GetAllocator());
  return WriteKVPair(key, array.Move());
}

ThreadConfigMgrError ThreadConfigManager::ReadConfigValueFromBinArray(const std::string& key,
                                                                      uint16_t index,
                                                                      uint8_t* value,
                                                                      size_t value_size,
                                                                      size_t* actual_size) const {
  ThreadConfigMgrError error = kThreadConfigMgrNoError;
  const std::lock_guard<std::mutex> read_lock(config_mutex_);
  otPlatLog(OT_LOG_LEVEL_DEBG, otLogRegion::OT_LOG_REGION_PLATFORM, "RapidJSONAllocator size: %d", config_.GetAllocator().Size());
  rapidjson::Value::MemberIterator it = config_.FindMember(key);

  if (it == config_.MemberEnd()) {
    return kThreadConfigMgrErrorConfigNotFound;
  }

  rapidjson::Value& config_value = it->value;

  if (config_value.IsNull()) {
    return kThreadConfigMgrErrorConfigNotFound;
  } else if (!config_value.IsArray()) {
    // An entry exists but it is not of appropriate type
    return kThreadConfigMgrErrorConflictingTypes;
  } else if (config_value.Size() <= index) {
    return kThreadConfigMgrErrorConfigNotFound;
  }

  // A call with actual_size == NULL means caller just wants to know if key
  // exists
  if (actual_size == NULL) {
    return kThreadConfigMgrNoError;
  }

  std::string string_value(config_value[index].GetString());
  const std::string decoded_value(modp_b64_decode(string_value));
  *actual_size = decoded_value.size();

  // Special case: a call with value == NULL means caller is interested in
  // just getting the size
  if (value == NULL) {
    return kThreadConfigMgrNoError;
  }

  size_t bytes_to_copy = *actual_size;
  // If buffer is smaller, we still copy the partial data
  if (bytes_to_copy > value_size) {
    bytes_to_copy = value_size;
    error = kThreadConfigMgrErrorBufferTooSmall;
  }
  memcpy(value, decoded_value.data(), bytes_to_copy);
  return error;
}

ThreadConfigMgrError ThreadConfigManager::ClearConfigValue(const std::string& key) {
  const std::lock_guard<std::mutex> write_lock(config_mutex_);
  if (!config_.HasMember(key)) {
    return kThreadConfigMgrErrorConfigNotFound;
  }
  config_.RemoveMember(key);
  return CommitKVPairs();
}

ThreadConfigMgrError ThreadConfigManager::ClearConfigValueFromArray(const std::string& key,
                                                                    uint16_t index) {
  const std::lock_guard<std::mutex> write_lock(config_mutex_);
  rapidjson::Value::MemberIterator it = config_.FindMember(key);
  if (it == config_.MemberEnd()) {
    return kThreadConfigMgrErrorConfigNotFound;
  }

  rapidjson::Value& config_value = it->value;

  if (!config_value.IsArray()) {
    return kThreadConfigMgrErrorConflictingTypes;
  } else if (config_value.Size() <= index) {
    return kThreadConfigMgrErrorConfigNotFound;
  }

  if (config_value.Size() == 1) {
    // Special case - only one member is present in the array,
    // erase the kv pair
    config_.RemoveMember(key);
  } else {
    config_value.Erase(config_value.Begin() + index);
  }

  return CommitKVPairs();
}

bool ThreadConfigManager::ConfigValueExists(const std::string& key) const {
  const std::lock_guard<std::mutex> read_lock(config_mutex_);
  rapidjson::Value::MemberIterator it = config_.FindMember(key);
  if (it == config_.MemberEnd() || (it->value).IsNull()) {
    return false;
  }
  return true;
}

ThreadConfigMgrError ThreadConfigManager::FactoryResetConfig() {
  const std::lock_guard<std::mutex> write_lock(config_mutex_);
  config_.RemoveAllMembers();
  return CommitKVPairs();
}

ThreadConfigMgrError ThreadConfigManager::ReadKVPair(const std::string& key,
                                                     rapidjson::Value& value) const {
  const std::lock_guard<std::mutex> read_lock(config_mutex_);
  rapidjson::Value::MemberIterator it = config_.FindMember(key);
  if (it == config_.MemberEnd()) {
    return kThreadConfigMgrErrorConfigNotFound;
  }
  rapidjson::Value& value_found = it->value;
  value.CopyFrom(value_found, config_.GetAllocator());
  return value.IsNull() ? kThreadConfigMgrErrorConfigNotFound : kThreadConfigMgrNoError;
}

ThreadConfigMgrError ThreadConfigManager::WriteKVPair(const std::string& key,
                                                      rapidjson::Value& value) {
  const std::lock_guard<std::mutex> write_lock(config_mutex_);
  rapidjson::Value key_value;
  key_value.SetString(key.c_str(), key.size(), config_.GetAllocator());
  rapidjson::Value::MemberIterator it = config_.FindMember(key_value);
  if (it != config_.MemberEnd()) {
    it->value = value;
  } else {
    config_.AddMember(key_value, value, config_.GetAllocator());
  }
  return CommitKVPairs();
}

ThreadConfigMgrError ThreadConfigManager::CommitKVPairs() {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  config_.Accept(writer);

  const std::string output(buffer.GetString());
  return files::WriteFile(config_store_path_.c_str(), output.c_str(), output.size())
             ? kThreadConfigMgrNoError
             : kThreadConfigMgrErrorPersistedStorageFail;
}
