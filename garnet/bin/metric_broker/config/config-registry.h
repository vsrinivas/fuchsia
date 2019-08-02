// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_CONFIG_REGISTRY_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_CONFIG_REGISTRY_H_

#include <string_view>
#include <unordered_map>

namespace broker_service {

// Services whose configuration can be handled.
enum class SupportedService {
  kCobaltService,
};

// Provides a registry for grouping and caching configurations in a per service based
// collection.
// This class provides a singleton instance for each Service.
template <SupportedService service, typename ConfigType>
class Registry {
 public:
  // Returns the process shared instance of |service| registry.
  static Registry* GetInstance() {
    static Registry instance = Registry();
    return &instance;
  }

  Registry() = default;
  Registry(const Registry&) = delete;
  Registry(Registry&&) noexcept = default;
  Registry& operator=(const Registry&) = delete;
  Registry& operator=(Registry&&) = delete;
  ~Registry() = default;

  // Returns a |ConfigType| for the corresponding |project_name|, if any.
  std::optional<const ConfigType*> Find(std::string_view project_name) const {
    auto it = project_name_to_config_.find(project_name);
    if (it == project_name_to_config_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  // Registers |config| for a project with |project_name|.
  void Register(std::string_view project_name, ConfigType config) {
    project_name_to_config_[project_name] = config;
  }

  // Removes any |ConfigType| mapped to project name.
  void Evict(std::string_view project_name) { project_name_to_config_.erase(project_name); }

  // Removes all registered |ConfigType|.
  void Clear() { project_name_to_config_.clear(); }

  // Const iterator to iterate over registered |ConfigType|s.
  auto begin() const { return project_name_to_config_.begin(); }
  auto end() const { return project_name_to_config_.end(); }

 private:
  std::unordered_map<std::string, ConfigType, std::less<>> project_name_to_config_;
};

}  // namespace broker_service

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_CONFIG_REGISTRY_H_
