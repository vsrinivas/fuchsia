// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_CONFIG_MANAGER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_CONFIG_MANAGER_H_

#include <mutex>
#include <string>

#include <Weave/Core/WeaveError.h>
#include <Weave/DeviceLayer/WeaveDeviceError.h>

#include "rapidjson/document.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

// Singleton class that accepts read/write calls to store key-value pairs.
//
// This class loads a JSON file from a fixed location and uses it as its backing
// store. Read requests are handled by quering the in-memory RapidJSON object,
// while writes update both the in-memory object and commits the JSON to disk.
//
class WeaveConfigManager {
 public:
  explicit WeaveConfigManager(const std::string& path);

  ~WeaveConfigManager() = default;
  WeaveConfigManager(const WeaveConfigManager&) = delete;
  WeaveConfigManager(WeaveConfigManager&&) = delete;
  WeaveConfigManager& operator=(const WeaveConfigManager&) = delete;
  WeaveConfigManager& operator=(WeaveConfigManager&&) = delete;

  WEAVE_ERROR ReadConfigValue(const std::string& key, bool* value);
  WEAVE_ERROR ReadConfigValue(const std::string& key, uint32_t* value);
  WEAVE_ERROR ReadConfigValue(const std::string& key, uint64_t* value);
  WEAVE_ERROR ReadConfigValueStr(const std::string& key, char* value, size_t value_size,
                                 size_t* out_size);
  WEAVE_ERROR ReadConfigValueBin(const std::string& key, uint8_t* value, size_t value_size,
                                 size_t* out_size);

  WEAVE_ERROR WriteConfigValue(const std::string& key, bool value);
  WEAVE_ERROR WriteConfigValue(const std::string& key, uint32_t value);
  WEAVE_ERROR WriteConfigValue(const std::string& key, uint64_t value);
  WEAVE_ERROR WriteConfigValueStr(const std::string& key, const char* value, size_t value_size);
  WEAVE_ERROR WriteConfigValueBin(const std::string& key, const uint8_t* value, size_t value_size);

  WEAVE_ERROR ClearConfigValue(const std::string& key);
  bool ConfigValueExists(const std::string& key);
  WEAVE_ERROR FactoryResetConfig();

 private:
  WeaveConfigManager();
  WEAVE_ERROR ReadKVPair(const std::string& key, rapidjson::Value& value);
  WEAVE_ERROR WriteKVPair(const std::string& key, rapidjson::Value& value);
  WEAVE_ERROR CommitKVPairs();

  friend WeaveConfigManager& WeaveConfigMgr(void);
  static WeaveConfigManager& GetInstance();

  rapidjson::Document config_;
  std::mutex config_mutex_;
};

// Convenience inline function to access the instance of WeaveConfigManager.
inline WeaveConfigManager& WeaveConfigMgr(void) { return WeaveConfigManager::GetInstance(); }

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_CONFIG_MANAGER_H_
