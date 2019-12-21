// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_LOADBENCH_WORKLOAD_H_
#define SRC_TESTING_LOADBENCH_WORKLOAD_H_

#include <lib/zx/time.h>

#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "action.h"
#include "object.h"
#include "rapidjson/document.h"
#include "src/lib/fxl/logging.h"

struct WorkerConfig {
  WorkerConfig() = default;
  WorkerConfig(WorkerConfig&&) = default;
  WorkerConfig& operator=(WorkerConfig&&) = default;

  WorkerConfig(const WorkerConfig& other) { *this = other; }
  WorkerConfig& operator=(const WorkerConfig& other) {
    if (this != &other) {
      name = other.name;
      group = other.group;
      priority = other.priority;
      for (auto& action : other.actions) {
        actions.emplace_back(action->Copy());
      }
    }
    return *this;
  }

  struct DeadlineParams {
    zx::duration capacity;
    zx::duration deadline;
    zx::duration period;
  };

  using PriorityType = std::variant<std::monostate, int, DeadlineParams>;

  std::string name;
  std::string group;
  PriorityType priority;
  std::vector<std::unique_ptr<Action>> actions;
};

// Represents the configuration and state parsed from a workload JSON
// definition file.
class Workload {
 public:
  static Workload Load(const std::string& path);

  Workload() = default;

  Workload(const Workload&) = delete;
  Workload& operator=(const Workload&) = delete;

  Workload(Workload&&) = default;
  Workload& operator=(Workload&&) = default;

  const auto& name() const { return name_; }
  const auto& priority() const { return priority_; }
  const auto& interval() const { return interval_; }

  auto& workers() { return workers_; }

 private:
  struct Duration {
    std::chrono::nanoseconds value;
  };

  struct Uniform {
    std::chrono::nanoseconds min;
    std::chrono::nanoseconds max;
  };

  enum AcceptNamedIntervalFlag {
    RejectNamedInterval = false,
    AcceptNamedInterval = true,
  };

  void Add(const std::string& name, std::unique_ptr<Object> object) {
    auto [iter, okay] = objects_.emplace(name, std::move(object));
    FXL_CHECK(okay) << "Object with name \"" << name << "\" defined more than once!";
  }

  Object& Get(const std::string& name) {
    const auto search = objects_.find(name);
    FXL_CHECK(search != objects_.end()) << "name=" << name;

    auto& [key, value] = *search;
    return *value;
  }

  template <typename T>
  T& Get(const std::string& name) {
    const auto search = objects_.find(name);
    FXL_CHECK(search != objects_.end()) << "name=" << name;

    auto& [key, value] = *search;
    FXL_CHECK(value->type() == T::Type) << "actual=" << value->type() << " expected=" << T::Type;

    return static_cast<T&>(*value);
  }

  void ParseObject(const std::string& name, const rapidjson::Value& object);
  Duration ParseDuration(const rapidjson::Value& object);
  Uniform ParseUniform(const rapidjson::Value& object);
  std::variant<Duration, Uniform> ParseInterval(const rapidjson::Value& object,
                                                AcceptNamedIntervalFlag accept_named_interval);
  void ParseNamedInterval(const std::string& name, const rapidjson::Value& object);
  zx::unowned_handle ParseTargetObjectAndGetHandle(const std::string& name,
                                                   const rapidjson::Value& object,
                                                   const std::string& context);
  std::unique_ptr<Action> ParseAction(const rapidjson::Value& action);
  void ParseNamedBehavior(const std::string& name, const rapidjson::Value& behavior);
  void ParseWorker(const rapidjson::Value& worker);

  std::string name_;
  std::optional<int> priority_;
  std::optional<std::chrono::nanoseconds> interval_;
  std::unordered_map<std::string, std::variant<Duration, Uniform>> intervals_;
  std::unordered_map<std::string, std::unique_ptr<Object>> objects_;
  std::unordered_map<std::string, std::unique_ptr<Action>> behaviors_;
  std::vector<WorkerConfig> workers_;
};

#endif  // SRC_TESTING_LOADBENCH_WORKLOAD_H_
