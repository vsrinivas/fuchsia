// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "workload.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zx/port.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <zircon/syscalls/port.h>

#include <chrono>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <utility>
#include <vector>

#include "action.h"
#include "object.h"
#include "random.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "src/lib/files/file.h"
#include "utility.h"
#include "worker.h"

struct SequenceAction : ActionBase<SequenceAction, ActionDefaultCopyable::False> {
  explicit SequenceAction(std::vector<std::unique_ptr<Action>> actions)
      : actions{std::move(actions)} {}

  void Perform(Worker* worker) override {
    for (auto& action : actions) {
      action->Perform(worker);
    }
  }

  std::unique_ptr<Action> Copy() const override {
    std::vector<std::unique_ptr<Action>> actions_copy;
    for (const auto& action : actions) {
      actions_copy.emplace_back(action->Copy());
    }
    return std::make_unique<SequenceAction>(std::move(actions_copy));
  }

  std::vector<std::unique_ptr<Action>> actions;
};

struct SleepDurationAction : ActionBase<SleepDurationAction> {
  explicit SleepDurationAction(std::chrono::nanoseconds duration_ns) : duration_ns{duration_ns} {}

  void Perform(Worker* worker) override { worker->Sleep(duration_ns); }

  const std::chrono::nanoseconds duration_ns;
};

struct SleepUniformAction : ActionBase<SleepUniformAction> {
  SleepUniformAction(std::chrono::nanoseconds min_ns, std::chrono::nanoseconds max_ns)
      : min_ns{min_ns}, max_ns{max_ns} {}

  void Perform(Worker* worker) override {
    const std::chrono::nanoseconds duration_ns{random.GetUniform(min_ns.count(), max_ns.count())};
    worker->Sleep(duration_ns);
  }

  const std::chrono::nanoseconds min_ns;
  const std::chrono::nanoseconds max_ns;
  Random random;
};

struct SpinDurationAction : ActionBase<SpinDurationAction> {
  explicit SpinDurationAction(std::chrono::nanoseconds duration_ns) : duration_ns{duration_ns} {}

  void Perform(Worker* worker) override { worker->Spin(duration_ns); }

  const std::chrono::nanoseconds duration_ns;
};

struct SpinUniformAction : ActionBase<SpinUniformAction> {
  SpinUniformAction(std::chrono::nanoseconds min_ns, std::chrono::nanoseconds max_ns)
      : min_ns{min_ns}, max_ns{max_ns} {}

  void Perform(Worker* worker) override {
    const std::chrono::nanoseconds duration_ns{random.GetUniform(min_ns.count(), max_ns.count())};
    worker->Spin(duration_ns);
  }

  const std::chrono::nanoseconds min_ns;
  const std::chrono::nanoseconds max_ns;
  Random random;
};

struct YieldAction : ActionBase<YieldAction> {
  void Perform(Worker* worker) override { worker->Yield(); }
};

struct ExitAction : ActionBase<ExitAction> {
  void Perform(Worker* worker) override { worker->Exit(); }
};

struct SetProfileAction : ActionBase<SetProfileAction> {
  SetProfileAction(zx::unowned_profile profile, bool once)
      : profile{std::move(profile)}, once{once} {}

  void Perform(Worker* worker) override {
    if (!once || !completed) {
      completed = true;
      worker->SetProfile(profile);
    }
  }

  zx::unowned_profile profile;
  const bool once;
  bool completed{false};
};

struct SetTimerAction : ActionBase<SetTimerAction> {
  SetTimerAction(TimerObject timer, std::chrono::nanoseconds relative_deadline_ns,
                 std::chrono::nanoseconds timer_slack_ns)
      : timer{timer}, relative_deadline_ns{relative_deadline_ns}, timer_slack_ns{timer_slack_ns} {}

  void Perform(Worker*) override {
    const zx_status_t status =
        timer->set(zx::deadline_after(zx::duration{relative_deadline_ns.count()}),
                   zx::duration{timer_slack_ns.count()});
    FX_CHECK(status == ZX_OK);
  }

  TimerObject timer;
  const std::chrono::nanoseconds relative_deadline_ns;
  const std::chrono::nanoseconds timer_slack_ns;
};

struct ChannelWriteAction : ActionBase<ChannelWriteAction> {
  ChannelWriteAction(ChannelObject channel, size_t side, size_t bytes)
      : channel{channel},
        endpoint{side == 0 ? channel->first : channel->second},
        bytes{bytes},
        buffer(bytes) {}

  void Perform(Worker*) override {
    const auto status = endpoint->write(0, buffer.data(), buffer.size(), nullptr, 0);
    FX_CHECK(status == ZX_OK);
  }

  ChannelObject channel;
  zx::unowned_channel endpoint;
  size_t bytes;
  std::vector<uint8_t> buffer;
};

struct ChannelReadAction : ActionBase<ChannelReadAction> {
  ChannelReadAction(ChannelObject channel, size_t side)
      : channel{channel},
        endpoint{side == 0 ? channel->first : channel->second},
        buffer(64 * 1024) {}

  void Perform(Worker*) override {
    uint32_t actual_bytes;
    uint32_t actual_handles;
    const auto status =
        endpoint->read(0, buffer.data(), nullptr, buffer.size(), 0, &actual_bytes, &actual_handles);
    FX_CHECK(status == ZX_OK) << "Failed to read channel: " << status;
  }

  ChannelObject channel;
  zx::unowned_channel endpoint;
  std::vector<uint8_t> buffer;
};

struct WaitOneAction : ActionBase<WaitOneAction> {
  WaitOneAction(zx::unowned_handle handle, zx_signals_t signals,
                std::optional<std::chrono::nanoseconds> relative_deadline_ns = std::nullopt)
      : handle{handle}, signals{signals}, relative_deadline_ns{relative_deadline_ns} {}

  void Perform(Worker*) {
    const auto absolute_deadline =
        relative_deadline_ns ? zx::deadline_after(zx::duration{relative_deadline_ns->count()})
                             : zx::time::infinite();
    const auto status = handle->wait_one(signals, absolute_deadline, nullptr);
    FX_CHECK(status == ZX_OK) << "Failed to signal object: " << status;
  }

  zx::unowned_handle handle;
  zx_signals_t signals;
  std::optional<std::chrono::nanoseconds> relative_deadline_ns;
};

struct WaitAsyncAction : ActionBase<WaitAsyncAction> {
  WaitAsyncAction(zx::unowned_port port, zx::unowned_handle handle, zx_signals_t signals)
      : port{port}, handle{handle}, signals{signals} {}

  void Perform(Worker*) override {
    const auto status = handle->wait_async(*port, 0, signals, 0);
    FX_CHECK(status == ZX_OK) << "Faild to wait async: " << status;
  }

  zx::unowned_port port;
  zx::unowned_handle handle;
  zx_signals_t signals;
};

struct PortWaitAction : ActionBase<PortWaitAction, ActionDefaultCopyable::False> {
  explicit PortWaitAction(
      PortObject port, std::optional<std::chrono::nanoseconds> relative_deadline_ns = std::nullopt)
      : port{port}, relative_deadline_ns{relative_deadline_ns} {
    RegisterTerminateEvent();
  }

  void RegisterTerminateEvent() {
    const auto status = PortObject::GetTerminateEvent()->wait_async(
        *port.object(), 0, PortObject::kTerminateSignal, 0);
    FX_CHECK(status == ZX_OK) << "Failed to wait async on terminate event: " << status;
  }

  void Perform(Worker*) override {
    zx_port_packet_t packet;
    const auto absolute_deadline =
        relative_deadline_ns ? zx::deadline_after(zx::duration{relative_deadline_ns->count()})
                             : zx::time::infinite();
    const auto status = port->wait(absolute_deadline, &packet);

    // TODO(eieio): Add option to fail or not on timeout.
    FX_CHECK(status == ZX_OK || status == ZX_ERR_TIMED_OUT) << "Failed to port wait: " << status;
  }

  std::unique_ptr<Action> Copy() const override {
    auto copy = std::make_unique<PortWaitAction>(*this);
    // Register an additional packet for every copy.
    copy->RegisterTerminateEvent();
    return copy;
  }

  PortObject port;
  const std::optional<std::chrono::nanoseconds> relative_deadline_ns;
};

struct ObjectSignalAction : ActionBase<ObjectSignalAction> {
  ObjectSignalAction(zx::unowned_handle handle, zx_signals_t clear_mask, zx_signals_t set_mask)
      : handle{handle}, clear_mask{clear_mask}, set_mask{set_mask} {}

  void Perform(Worker*) {
    const auto status = handle->signal(clear_mask, set_mask);
    FX_CHECK(status == ZX_OK) << "Failed to signal object: " << status;
  }

  zx::unowned_handle handle;
  zx_signals_t clear_mask;
  zx_signals_t set_mask;
};

// Proxies an iterator over the members of the given value node. As of this
// writing, the version of rapidjson in third_party does not support range-based
// for loops. This adapter provides the missing functionality.
struct IterateMembers {
  explicit IterateMembers(const rapidjson::Value& value)
      : begin_iterator{value.MemberBegin()}, end_iterator{value.MemberEnd()} {}

  auto begin() { return begin_iterator; }
  auto end() { return end_iterator; }

  rapidjson::Value::ConstMemberIterator begin_iterator;
  rapidjson::Value::ConstMemberIterator end_iterator;
};

// Proxies an iterator over the values of the given array node. Provides missing
// functionality similar to the iterator above.
struct IterateValues {
  explicit IterateValues(const rapidjson::Value& value)
      : begin_iterator{value.Begin()}, end_iterator{value.End()} {}

  auto begin() { return begin_iterator; }
  auto end() { return end_iterator; }

  rapidjson::Value::ConstValueIterator begin_iterator;
  rapidjson::Value::ConstValueIterator end_iterator;
};

template <typename... Context>
const auto& GetMember(const char* name, const rapidjson::Value& object, Context&&... context) {
  FX_CHECK(object.IsObject())
      << (std::ostringstream{} << ... << std::forward<Context>(context)).rdbuf()
      << " must be a JSON object!";
  FX_CHECK(object.HasMember(name))
      << (std::ostringstream{} << ... << std::forward<Context>(context)).rdbuf()
      << " must have a \"" << name << "\" member!";
  return object[name];
}

template <typename... Context>
auto GetInt(const char* name, const rapidjson::Value& object, Context&&... context) {
  const auto& member = GetMember(name, object, std::forward<Context>(context)...);
  FX_CHECK(member.IsInt())
      << (std::ostringstream{} << ... << std::forward<Context>(context)).rdbuf() << " member \""
      << name << "\" must be an integer!";
  return member.GetInt();
}

template <typename... Context>
const auto* GetString(const char* name, const rapidjson::Value& object, Context&&... context) {
  const auto& member = GetMember(name, object, std::forward<Context>(context)...);
  FX_CHECK(member.IsString())
      << (std::ostringstream{} << ... << std::forward<Context>(context)).rdbuf() << " member \""
      << name << "\" must be a string!";
  return member.GetString();
}

template <typename... Context>
const auto& GetArray(const char* name, const rapidjson::Value& object, Context&&... context) {
  const auto& member = GetMember(name, object, std::forward<Context>(context)...);
  FX_CHECK(member.IsArray())
      << (std::ostringstream{} << ... << std::forward<Context>(context)).rdbuf() << " member \""
      << name << "\" must be an array!";
  return member;
}

template <typename... Context>
const auto& GetObject(const char* name, const rapidjson::Value& object, Context&&... context) {
  const auto& member = GetMember(name, object, std::forward<Context>(context)...);
  FX_CHECK(member.IsObject())
      << (std::ostringstream{} << ... << std::forward<Context>(context)).rdbuf() << " member \""
      << name << "\" must be a JSON object!";
  return member;
}

template <typename... Context>
auto GetUint(const char* name, const rapidjson::Value& object, Context&&... context) {
  const auto& member = GetMember(name, object, std::forward<Context>(context)...);
  FX_CHECK(member.IsUint())
      << (std::ostringstream{} << ... << std::forward<Context>(context)).rdbuf() << " member \""
      << name << "\" must be an unsigned integer!";
  return member.GetUint();
}

void Workload::ParseObject(const std::string& name, const rapidjson::Value& object) {
  const std::string type_string = GetString("type", object, "Named object \"", name, "\"");
  if (type_string == "timer") {
    Add(name, TimerObject::Create());
  } else if (type_string == "port") {
    Add(name, PortObject::Create());
  } else if (type_string == "channel") {
    Add(name, ChannelObject::Create());
  } else if (type_string == "event") {
    Add(name, EventObject::Create());
  } else {
    FX_CHECK(false) << "Object \"" << name << "\" has unknown type \"" << type_string << "\"!";
  }
}

Workload::Duration Workload::ParseDuration(const rapidjson::Value& object) {
  if (object.IsInt()) {
    return {std::chrono::nanoseconds{object.GetInt()}};
  } else if (object.IsString()) {
    return {ParseDurationString(object.GetString())};
  } else {
    FX_CHECK(false) << "Duration must be an integer or string!";
    __builtin_unreachable();
  }
}

Workload::Uniform Workload::ParseUniform(const rapidjson::Value& object) {
  auto [min_ns] = ParseDuration(GetMember("min", object, "Uniform object"));
  auto [max_ns] = ParseDuration(GetMember("max", object, "Uniform object"));

  return {min_ns, max_ns};
}

std::variant<Workload::Duration, Workload::Uniform> Workload::ParseInterval(
    const rapidjson::Value& object, AcceptNamedIntervalFlag accept_named_interval) {
  FX_CHECK(object.IsObject()) << "Interval must be a JSON object!";

  const bool has_duration = object.HasMember("duration");
  const bool has_uniform = object.HasMember("uniform");
  const bool has_interval = object.HasMember("interval");

  FX_CHECK(accept_named_interval || !has_interval)
      << "Timespec \"interval\" is not supported in this context!";

  FX_CHECK((has_duration + has_uniform + has_interval) == 1)
      << "Interval must have exactly one timespec: either \"uniform\" or "
         "\"duration\""
      << (accept_named_interval ? " or \"interval\"" : "") << "!";

  if (has_duration) {
    return {ParseDuration(object["duration"])};
  } else if (has_uniform) {
    return {ParseUniform(object["uniform"])};
  } else if (has_interval) {
    const auto* interval_name_string = GetString("interval", object, "Interval");
    auto search = intervals_.find(interval_name_string);
    FX_CHECK(search != intervals_.end())
        << "Undefined named interval \"" << interval_name_string << "\"!";
    return search->second;
  } else {
    __builtin_unreachable();
  }
}

void Workload::ParseNamedInterval(const std::string& name, const rapidjson::Value& object) {
  FX_CHECK(object.IsObject()) << "Named interval must be a JSON object!";

  const auto result = ParseInterval(object, RejectNamedInterval);
  auto [iter, okay] = intervals_.emplace(name, result);
  FX_CHECK(okay) << "Named interval \"" << name << "\" defined more than once!";
}

zx::unowned_handle Workload::ParseTargetObjectAndGetHandle(const std::string& name,
                                                           const rapidjson::Value& object,
                                                           const std::string& context) {
  Object& target = Get(name);
  switch (target.type()) {
    case TimerObject::Type:
      return zx::unowned_handle{static_cast<TimerObject&>(target)->get()};
    case ChannelObject::Type: {
      const size_t side = GetInt("side", object, context);
      FX_CHECK(side == 0 || side == 1)
          << "Wait async action member \"side\" must be an integer value 0 or 1!";
      auto [first, second] = static_cast<ChannelObject&>(target).bind();
      return zx::unowned_handle{side == 0 ? first->get() : second->get()};
    }
    case EventObject::Type:
      return zx::unowned_handle{static_cast<EventObject&>(target)->get()};
    case PortObject::Type:
      return zx::unowned_handle{static_cast<PortObject&>(target)->get()};
    default:
      FX_CHECK(false) << "Unknown object type: " << target.type();
      __builtin_unreachable();
  }
}

std::unique_ptr<Action> Workload::ParseAction(const rapidjson::Value& action) {
  const std::string action_string = GetString("action", action, "Action");
  if (action_string == "spin") {
    const auto result = ParseInterval(action, AcceptNamedInterval);
    if (std::holds_alternative<Duration>(result)) {
      const auto [duration_ns] = std::get<Duration>(result);
      return SpinDurationAction::Create(duration_ns);
    } else /*if (std::holds_alternative<Uniform>(result))*/ {
      const auto [min_ns, max_ns] = std::get<Uniform>(result);
      return SpinUniformAction::Create(min_ns, max_ns);
    }
  } else if (action_string == "sleep") {
    const auto result = ParseInterval(action, AcceptNamedInterval);
    if (std::holds_alternative<Duration>(result)) {
      const auto [duration_ns] = std::get<Duration>(result);
      return SleepDurationAction::Create(duration_ns);
    } else /*if (std::holds_alternative<Uniform>(result))*/ {
      const auto [min_ns, max_ns] = std::get<Uniform>(result);
      return SleepUniformAction::Create(min_ns, max_ns);
    }
  } else if (action_string == "yield") {
    return YieldAction::Create();
  } else if (action_string == "write") {
    const auto* channel_name = GetString("channel", action, "Write action");
    const size_t side = GetInt("side", action, "Write action");
    const size_t bytes = GetInt("bytes", action, "Write action");
    return ChannelWriteAction::Create(Get<ChannelObject>(channel_name), side, bytes);
  } else if (action_string == "read") {
    const auto* channel_name = GetString("channel", action, "Read action");
    const size_t side = GetInt("side", action, "Read action");
    return ChannelReadAction::Create(Get<ChannelObject>(channel_name), side);
  } else if (action_string == "behavior") {
    const auto* behavior_name = GetString("name", action, "Behavior action");
    auto search = behaviors_.find(behavior_name);
    FX_CHECK(search != behaviors_.end()) << "Unknown named behavior \"" << behavior_name << "\"!";
    return search->second->Copy();
  } else if (action_string == "wait_async") {
    const auto* context = "Wait async action";
    const auto* port_name = GetString("port", action, context);
    const auto* object_name = GetString("object", action, context);
    const auto signals = GetInt("signals", action, context);

    auto& port_object = Get<PortObject>(port_name);
    zx::unowned_handle handle = ParseTargetObjectAndGetHandle(object_name, action, context);

    return WaitAsyncAction::Create(zx::unowned_port{port_object->get()}, std::move(handle),
                                   signals);
  } else if (action_string == "wait_one") {
    const auto* context = "Wait one action";
    const auto* object_name = GetString("object", action, context);
    const auto signals = GetInt("signals", action, context);

    std::optional<std::chrono::nanoseconds> relative_deadline_ns;
    if (action.HasMember("deadline")) {
      relative_deadline_ns = ParseDuration(action["deadline"]).value;
    }

    zx::unowned_handle handle = ParseTargetObjectAndGetHandle(object_name, action, context);

    return WaitOneAction::Create(std::move(handle), signals, relative_deadline_ns);
  } else if (action_string == "port_wait") {
    const auto* port_name = GetString("port", action, "Port wait action");

    std::optional<std::chrono::nanoseconds> relative_deadline_ns;
    if (action.HasMember("deadline")) {
      relative_deadline_ns = ParseDuration(action["deadline"]).value;
    }

    return PortWaitAction::Create(Get<PortObject>(port_name), relative_deadline_ns);
  } else if (action_string == "signal") {
    const auto* context = "Signal action";
    const auto* object_name = GetString("object", action, context);
    const auto clear_mask = GetInt("clear", action, context);
    const auto set_mask = GetInt("set", action, context);

    zx::unowned_handle handle = ParseTargetObjectAndGetHandle(object_name, action, context);

    return ObjectSignalAction::Create(std::move(handle), clear_mask, set_mask);
  } else if (action_string == "timer_set") {
    const auto* timer_name = GetString("timer", action, "Timer set action");
    const auto relative_deadline_ns = ParseDuration(action["deadline"]).value;
    const auto timer_slack_ns = action.HasMember("slack") ? ParseDuration(action["slack"]).value
                                                          : std::chrono::nanoseconds{0};

    auto& timer_object = Get<TimerObject>(timer_name);

    return SetTimerAction::Create(timer_object, relative_deadline_ns, timer_slack_ns);
  } else if (action_string == "exit") {
    return ExitAction::Create();
  } else {
    FX_CHECK(false) << "Unknown action \"" << action_string << "\"!";
    __builtin_unreachable();
  }
}

void Workload::ParseNamedBehavior(const std::string& name, const rapidjson::Value& behavior) {
  if (behavior.IsObject()) {
    auto [iter, okay] = behaviors_.emplace(name, ParseAction(behavior));
    FX_CHECK(okay) << "Behavior \"" << name << "\" defined more than once!";
  } else if (behavior.IsArray()) {
    std::vector<std::unique_ptr<Action>> actions;
    for (const auto& action : IterateValues(behavior)) {
      actions.emplace_back(ParseAction(action));
    }

    auto [iter, okay] = behaviors_.emplace(name, SequenceAction::Create(std::move(actions)));
    FX_CHECK(okay) << "Behavior \"" << name << "\" defined more than once!";
  } else {
    FX_CHECK(false) << "Behavior \"" << name << "\" must be a JSON object or array!";
  }
}

void Workload::ParseWorker(const rapidjson::Value& worker) {
  FX_CHECK(worker.IsObject()) << "Worker must be a JSON object!";

  WorkerConfig config;

  if (worker.HasMember("name")) {
    config.name = GetString("name", worker, "Worker");
  }

  if (worker.HasMember("group")) {
    config.group = GetString("group", worker, "Worker");
  }

  if (worker.HasMember("priority")) {
    const auto& priority_member = worker["priority"];
    const bool is_int = priority_member.IsInt();
    const bool is_object = priority_member.IsObject();
    FX_CHECK(is_int || is_object)
        << "Worker member \"priority\" must either be an integer or a JSON object!";

    if (is_int) {
      config.priority = GetInt("priority", worker, "Worker");
    } else if (is_object) {
      const bool has_capacity = priority_member.HasMember("capacity");
      const bool has_deadline = priority_member.HasMember("deadline");
      const bool has_period = priority_member.HasMember("period");
      FX_CHECK(has_capacity && has_deadline && has_period)
          << "Worker member \"priority\" must have members \"capacity\", \"deadline\", and "
             "\"period\"!";

      const zx::duration capacity{ParseDuration(priority_member["capacity"]).value.count()};
      const zx::duration deadline{ParseDuration(priority_member["deadline"]).value.count()};
      const zx::duration period{ParseDuration(priority_member["period"]).value.count()};

      config.priority = WorkerConfig::DeadlineParams{capacity, deadline, period};
    }
  }

  if (worker.HasMember("actions")) {
    const auto& actions_member = worker["actions"];
    const bool is_array = actions_member.IsArray();
    const bool is_string = actions_member.IsString();
    FX_CHECK(is_array || is_string)
        << "Worker member \"actions\" must either be a string or a JSON object!";

    if (is_array) {
      const auto& actions = GetArray("actions", worker, "Worker");
      for (const auto& action : IterateValues(actions)) {
        config.actions.emplace_back(ParseAction(action));
      }
    } else if (is_string) {
      const auto* behavior_name = GetString("actions", worker, "Worker");
      auto search = behaviors_.find(behavior_name);
      FX_CHECK(search != behaviors_.end()) << "Unknown named behavior \"" << behavior_name << "\"!";
      config.actions.emplace_back(search->second->Copy());
    }
  }

  int instances = 1;
  if (worker.HasMember("instances")) {
    const auto& instances_member = worker["instances"];
    const bool is_integer = instances_member.IsInt();
    const bool is_string = instances_member.IsString();
    const bool is_object = instances_member.IsObject();
    FX_CHECK(is_integer || is_string || is_object)
        << "Worker member \"instances\" must either be an integer, string or a JSON object!";

    if (is_integer) {
      instances = GetInt("instances", worker, "Worker");
    } else if (is_string) {
      instances = ParseInstancesString(GetString("instances", worker, "Worker"));
    } else if (is_object) {
      FX_CHECK(false) << "Worker member \"instances\" expressions are not yet implemented!";
    }
  }

  if (instances <= 0) {
    FX_LOGS(WARNING) << "Worker configured with instances=" << instances << "!";
  }

  for (int i = 0; i < instances; i++) {
    workers_.emplace_back(config);
  }
}

void Workload::ParseTracing(const rapidjson::Value& tracing) {
  FX_CHECK(tracing.IsObject()) << "Tracing configuration must be a JSON object!";

  TracingConfig config;

  if (tracing.HasMember("group mask")) {
    const auto& group_mask = GetMember("group mask", tracing, "Tracing");

    if (group_mask.IsUint()) {
      config.group_mask = GetUint("group mask", tracing, "Tracing");
    } else if (group_mask.IsString()) {
      const std::string group_mask_str = GetString("group mask", tracing, "Tracing");

      if (group_mask_str == "KTRACE_GRP_ALL") {
        config.group_mask = KTRACE_GRP_ALL;
      } else if (group_mask_str == "KTRACE_GRP_META") {
        config.group_mask = KTRACE_GRP_META;
      } else if (group_mask_str == "KTRACE_GRP_LIFECYCLE") {
        config.group_mask = KTRACE_GRP_LIFECYCLE;
      } else if (group_mask_str == "KTRACE_GRP_SCHEDULER") {
        config.group_mask = KTRACE_GRP_SCHEDULER;
      } else if (group_mask_str == "KTRACE_GRP_TASKS") {
        config.group_mask = KTRACE_GRP_TASKS;
      } else if (group_mask_str == "KTRACE_GRP_IPC") {
        config.group_mask = KTRACE_GRP_IPC;
      } else if (group_mask_str == "KTRACE_GRP_IRQ") {
        config.group_mask = KTRACE_GRP_IRQ;
      } else if (group_mask_str == "KTRACE_GRP_PROBE") {
        config.group_mask = KTRACE_GRP_PROBE;
      } else if (group_mask_str == "KTRACE_GRP_ARCH") {
        config.group_mask = KTRACE_GRP_ARCH;
      } else if (group_mask_str == "KTRACE_GRP_SYSCALL") {
        config.group_mask = KTRACE_GRP_SYSCALL;
      } else if (group_mask_str == "KTRACE_GRP_VM") {
        config.group_mask = KTRACE_GRP_VM;
      } else {
        FX_LOGS(WARNING) << "Tracing enabled with unknown group mask, mask set to all groups.";
        config.group_mask = KTRACE_GRP_ALL;
      }
    } else {
      FX_CHECK(false) << "Tracing group mask must be an unsigned integer or string!";
      __builtin_unreachable();
    }
  } else /*Set default tracing group mask*/ {
    FX_LOGS(WARNING) << "Tracing enabled with no group mask specified, mask set to all groups.";
    config.group_mask = KTRACE_GRP_ALL;
  }

  if (tracing.HasMember("filepath")) {
    config.filepath = GetString("filepath", tracing, "Tracing");
  }

  if (tracing.HasMember("string ref")) {
    config.trace_string_ref = GetString("string ref", tracing, "Tracing");
  }

  tracing_ = config;
}

void GetLineAndColumnForOffset(const std::string& input, size_t offset, int32_t* output_line,
                               int32_t* output_column) {
  if (offset == 0) {
    // Errors at position 0 are assumed to be related to the whole file.
    *output_line = 0;
    *output_column = 0;
    return;
  }
  *output_line = 1;
  *output_column = 1;
  for (size_t i = 0; i < input.size() && i < offset; i++) {
    if (input[i] == '\n') {
      *output_line += 1;
      *output_column = 1;
    } else {
      *output_column += 1;
    }
  }
}

std::string GetErrorMessage(const rapidjson::Document& document, const std::string& file_data) {
  int32_t line;
  int32_t column;
  GetLineAndColumnForOffset(file_data, document.GetErrorOffset(), &line, &column);

  std::ostringstream stream;
  stream << "at " << line << ":" << column << ": " << GetParseError_En(document.GetParseError());
  return stream.str();
}

Workload Workload::Load(const std::string& path) {
  std::string file_data;
  FX_CHECK(files::ReadFileToString(path, &file_data))
      << "Failed to read workload config file \"" << path << "\"!";

  rapidjson::Document document;

  const auto kFlags = rapidjson::kParseCommentsFlag | rapidjson::kParseTrailingCommasFlag;
  document.Parse<kFlags>(file_data);

  FX_CHECK(!document.HasParseError()) << "Error parsing workload config file \"" << path << "\" "
                                      << GetErrorMessage(document, file_data) << "!";
  FX_CHECK(document.IsObject()) << "Document must be a JSON object!";

  Workload workload;

  // Handle workload name.
  if (document.HasMember("name")) {
    workload.name_ = GetString("name", document, "Workload");
  }

  // Handle global config.
  if (document.HasMember("config")) {
    const auto& config = GetObject("config", document, "Workload");
    if (config.HasMember("priority")) {
      workload.priority_ = GetInt("priority", config, "Workload config");
    }

    if (config.HasMember("interval")) {
      const auto& interval = config["interval"];
      auto [duration_ns] = workload.ParseDuration(interval);
      workload.interval_ = duration_ns;
    }
  }

  // Handle named intervals.
  if (document.HasMember("intervals")) {
    const auto& intervals = GetObject("intervals", document, "Workload");
    for (const auto& interval : IterateMembers(intervals)) {
      workload.ParseNamedInterval(interval.name.GetString(), interval.value);
    }
  }

  // Handle global objects.
  if (document.HasMember("objects")) {
    const auto& objects = GetObject("objects", document, "Workload");
    for (const auto& object : IterateMembers(objects)) {
      workload.ParseObject(object.name.GetString(), object.value);
    }
  }

  // Handle named actions.
  if (document.HasMember("behaviors")) {
    const auto& behaviors = GetObject("behaviors", document, "Workload");
    for (const auto& behavior : IterateMembers(behaviors)) {
      workload.ParseNamedBehavior(behavior.name.GetString(), behavior.value);
    }
  }

  // Handle workers.
  if (document.HasMember("workers")) {
    const auto& workers = GetArray("workers", document, "Workload");
    for (const auto& worker : IterateValues(workers)) {
      workload.ParseWorker(worker);
    }
  }

  // Handle tracing.
  if (document.HasMember("tracing")) {
    const auto& tracing = GetObject("tracing", document, "Workload");
    workload.ParseTracing(tracing);
  }

  return workload;
}
