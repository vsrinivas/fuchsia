// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_THREAD_CONFIG_MANAGER_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_THREAD_CONFIG_MANAGER_H_

#include <mutex>
#include <string>

#include "rapidjson/document.h"

enum ThreadConfigMgrError {
  kThreadConfigMgrNoError = 0,
  kThreadConfigMgrErrorConfigNotFound = -1,
  kThreadConfigMgrErrorPersistedStorageFail = -2,
  kThreadConfigMgrErrorBufferTooSmall = -3,
  kThreadConfigMgrErrorConflictingTypes = -4,
};

constexpr char kThreadSettingsPath[] = "/data/thread-settings.json";

// Abstract class that defines the reader interface for the config store.
class ThreadConfigReader {
 public:
  virtual ~ThreadConfigReader() = default;
  virtual ThreadConfigMgrError ReadConfigValueFromBinArray(const std::string& key, uint16_t index,
                                                           uint8_t* value, size_t value_size,
                                                           size_t* out_size) const = 0;
  virtual bool ConfigValueExists(const std::string& key) const = 0;
};

// Abstract class that defines the writer interface for the config store.
class ThreadConfigWriter {
 public:
  virtual ~ThreadConfigWriter() = default;
  virtual ThreadConfigMgrError ClearConfigValue(const std::string& key) = 0;
  virtual ThreadConfigMgrError ClearConfigValueFromArray(const std::string& key,
                                                         uint16_t index) = 0;
  virtual ThreadConfigMgrError AppendConfigValueBinArray(const std::string& key,
                                                         const uint8_t* value,
                                                         size_t value_size) = 0;
  virtual ThreadConfigMgrError FactoryResetConfig() = 0;
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
class ThreadConfigManager : public ThreadConfigReader, ThreadConfigWriter {
 public:
  explicit ThreadConfigManager(const std::string& path);

  ~ThreadConfigManager() = default;
  ThreadConfigManager(const ThreadConfigManager&) = delete;
  ThreadConfigManager(ThreadConfigManager&&) = delete;
  ThreadConfigManager& operator=(const ThreadConfigManager&) = delete;
  ThreadConfigManager& operator=(ThreadConfigManager&&) = delete;

  // Read the config with key as 'key' at index 'index', and stores value in
  // 'value', and its size in 'out_size'
  // If out_size == NULL, simply returns the presence test - whether value
  // exists or not
  // If out_size != NULL but value == NULL, return that value is present, with
  // the size in out_size
  ThreadConfigMgrError ReadConfigValueFromBinArray(const std::string& key, uint16_t index,
                                                   uint8_t* value, size_t value_size,
                                                   size_t* out_size) const override;
  bool ConfigValueExists(const std::string& key) const override;

  ThreadConfigMgrError AppendConfigValueBinArray(const std::string& key, const uint8_t* value,
                                                 size_t value_size) override;
  ThreadConfigMgrError ClearConfigValue(const std::string& key) override;
  ThreadConfigMgrError ClearConfigValueFromArray(const std::string& key, uint16_t index) override;
  ThreadConfigMgrError FactoryResetConfig() override;

  // Returns a read-only interface to this instance of ThreadConfigManager.
  static std::unique_ptr<ThreadConfigReader> CreateReadOnlyInstance(const std::string& path) {
    return std::make_unique<ThreadConfigManager>(path);
  }

 private:
  ThreadConfigMgrError ReadKVPair(const std::string& key, rapidjson::Value& value) const;
  ThreadConfigMgrError WriteKVPair(const std::string& key, rapidjson::Value& value);
  ThreadConfigMgrError CommitKVPairs();

  const std::string config_store_path_;
  mutable rapidjson::Document config_;
  mutable std::mutex config_mutex_;
};

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_THREAD_CONFIG_MANAGER_H_
