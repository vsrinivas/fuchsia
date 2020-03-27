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

// Abstract class that defines the reader interface for the config store.
class WeaveConfigReader {
 public:
  virtual ~WeaveConfigReader() = default;
  virtual WEAVE_ERROR ReadConfigValue(const std::string& key, bool* value) const = 0;
  virtual WEAVE_ERROR ReadConfigValue(const std::string& key, uint16_t* value) const = 0;
  virtual WEAVE_ERROR ReadConfigValue(const std::string& key, uint32_t* value) const = 0;
  virtual WEAVE_ERROR ReadConfigValue(const std::string& key, uint64_t* value) const = 0;
  virtual WEAVE_ERROR ReadConfigValueStr(const std::string& key, char* value, size_t value_size,
                                         size_t* out_size) const = 0;
  virtual WEAVE_ERROR ReadConfigValueBin(const std::string& key, uint8_t* value, size_t value_size,
                                         size_t* out_size) const = 0;
  virtual bool ConfigValueExists(const std::string& key) const = 0;
};

// Abstract class that defines the writer interface for the config store.
class WeaveConfigWriter {
 public:
  virtual ~WeaveConfigWriter() = default;
  virtual WEAVE_ERROR WriteConfigValue(const std::string& key, bool value) = 0;
  virtual WEAVE_ERROR WriteConfigValue(const std::string& key, uint32_t value) = 0;
  virtual WEAVE_ERROR WriteConfigValue(const std::string& key, uint64_t value) = 0;
  virtual WEAVE_ERROR WriteConfigValueStr(const std::string& key, const char* value,
                                          size_t value_size) = 0;
  virtual WEAVE_ERROR WriteConfigValueBin(const std::string& key, const uint8_t* value,
                                          size_t value_size) = 0;
  virtual WEAVE_ERROR ClearConfigValue(const std::string& key) = 0;
  virtual WEAVE_ERROR FactoryResetConfig() = 0;
};

// Singleton class that accepts read/write calls to store key-value pairs.
//
// This class loads a JSON file from a fixed location and uses it as its backing
// store. Read requests are handled by quering the in-memory RapidJSON object,
// while writes update both the in-memory object and commits the JSON to disk.
//
// It is up to callers of this class to not open the same path across multiple
// instances, to prevent R/W interleaving and corrupting the backing file.
//
class WeaveConfigManager : public WeaveConfigReader, WeaveConfigWriter {
 public:
  explicit WeaveConfigManager(const std::string& path);

  ~WeaveConfigManager() = default;
  WeaveConfigManager(const WeaveConfigManager&) = delete;
  WeaveConfigManager(WeaveConfigManager&&) = delete;
  WeaveConfigManager& operator=(const WeaveConfigManager&) = delete;
  WeaveConfigManager& operator=(WeaveConfigManager&&) = delete;

  WEAVE_ERROR ReadConfigValue(const std::string& key, bool* value) const override;
  WEAVE_ERROR ReadConfigValue(const std::string& key, uint16_t* value) const override;
  WEAVE_ERROR ReadConfigValue(const std::string& key, uint32_t* value) const override;
  WEAVE_ERROR ReadConfigValue(const std::string& key, uint64_t* value) const override;
  WEAVE_ERROR ReadConfigValueStr(const std::string& key, char* value, size_t value_size,
                                 size_t* out_size) const override;
  WEAVE_ERROR ReadConfigValueBin(const std::string& key, uint8_t* value, size_t value_size,
                                 size_t* out_size) const override;

  bool ConfigValueExists(const std::string& key) const override;

  WEAVE_ERROR WriteConfigValue(const std::string& key, bool value) override;
  WEAVE_ERROR WriteConfigValue(const std::string& key, uint32_t value) override;
  WEAVE_ERROR WriteConfigValue(const std::string& key, uint64_t value) override;
  WEAVE_ERROR WriteConfigValueStr(const std::string& key, const char* value,
                                  size_t value_size) override;
  WEAVE_ERROR WriteConfigValueBin(const std::string& key, const uint8_t* value,
                                  size_t value_size) override;

  WEAVE_ERROR ClearConfigValue(const std::string& key) override;
  WEAVE_ERROR FactoryResetConfig() override;

  // Returns a read-only interface to this instance of WeaveConfigManager.
  static std::unique_ptr<WeaveConfigReader> CreateReadOnlyInstance(const std::string& path) {
    return std::make_unique<WeaveConfigManager>(path);
  }

 private:
  WeaveConfigManager();
  WEAVE_ERROR ReadKVPair(const std::string& key, rapidjson::Value& value) const;
  WEAVE_ERROR WriteKVPair(const std::string& key, rapidjson::Value& value);
  WEAVE_ERROR CommitKVPairs();

  friend WeaveConfigManager& WeaveConfigMgr(void);
  static WeaveConfigManager& GetInstance();

  const std::string config_store_path_;
  mutable rapidjson::Document config_;
  mutable std::mutex config_mutex_;
};

// Convenience inline function to access the instance of WeaveConfigManager.
inline WeaveConfigManager& WeaveConfigMgr(void) { return WeaveConfigManager::GetInstance(); }

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_WEAVE_CONFIG_MANAGER_H_
