// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/logging.h>

#include <set>

#include "src/connectivity/overnet/overnetstack/fuchsia_port.h"
#include "src/connectivity/overnet/overnetstack/mdns.h"
#include "src/connectivity/overnet/overnetstack/omdp_nub.h"
#include "src/connectivity/overnet/overnetstack/overnet_app.h"
#include "src/connectivity/overnet/overnetstack/service.h"
#include "src/connectivity/overnet/overnetstack/udp_nub.h"

namespace overnetstack {

class FuchsiaTimer final : public overnet::Timer {
 public:
  overnet::TimeStamp Now() override {
    return ToTimeStamp(async::Now(dispatcher_));
  }

 private:
  async_dispatcher_t* dispatcher_ = async_get_default_dispatcher();

  struct Task : public async_task_t {
    overnet::Timeout* timeout;
  };

  static void TaskHandler(async_dispatcher_t* async, async_task_t* task,
                          zx_status_t status) {
    FireTimeout(static_cast<Task*>(task)->timeout,
                overnet::Status::FromZx(status));
  }

  void InitTimeout(overnet::Timeout* timeout,
                   overnet::TimeStamp when) override {
    auto* async_timeout = TimeoutStorage<Task>(timeout);
    async_timeout->state = {ASYNC_STATE_INIT};
    async_timeout->handler = TaskHandler;
    async_timeout->deadline = FromTimeStamp(when).get();
    async_timeout->timeout = timeout;
    if (async_post_task(dispatcher_, async_timeout) != ZX_OK) {
      FireTimeout(timeout, overnet::Status::Cancelled());
    }
  }
  void CancelTimeout(overnet::Timeout* timeout,
                     overnet::Status status) override {
    if (async_cancel_task(dispatcher_, TimeoutStorage<Task>(timeout)) ==
        ZX_OK) {
      FireTimeout(timeout, overnet::Status::Cancelled());
    }
  }
};

class FuchsiaLog final : public overnet::TraceRenderer {
 public:
  FuchsiaLog(overnet::Timer* timer) : timer_(timer) {}
  void Render(overnet::TraceOutput output) override {
    auto severity = [sev = output.severity] {
      switch (sev) {
        case overnet::Severity::DEBUG:
          return -2;
        case overnet::Severity::TRACE:
          return -1;
        case overnet::Severity::INFO:
          return fxl::LOG_INFO;
        case overnet::Severity::WARNING:
          return fxl::LOG_WARNING;
        case overnet::Severity::ERROR:
          return fxl::LOG_ERROR;
      }
    }();
    fxl::LogMessage message(severity, output.file, output.line, nullptr);
    message.stream() << timer_->Now() << " " << output.message;
    bool annotated = false;
    auto maybe_begin_annotation = [&] {
      if (annotated) {
        return;
      }
      annotated = true;
      message.stream() << " //";
    };
    output.scopes.Visit([&](overnet::Module module, void* ptr) {
      maybe_begin_annotation();
      message.stream() << ' ' << module << ':' << ptr;
    });
  }
  void NoteParentChild(overnet::Op, overnet::Op) override {}

 private:
  overnet::Timer* const timer_;
};

static std::string Trim(std::string s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                  [](int ch) { return !std::isspace(ch); }));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](int ch) { return !std::isspace(ch); })
              .base(),
          s.end());
  return s;
}

class Configuration {
 public:
  overnet::Severity trace_severity = overnet::Severity::INFO;
  std::set<std::string> modules = {"udp", "mdns_advertiser", "mdns_subscriber",
                                   "omdp"};
  bool help = false;

  bool ParseCommandLine(int argc, const char** argv);

  // If a module is in the list, remove it and return true, otherwise return
  // false.
  bool TakeModule(const std::string& name) {
    auto it = modules.find(name);
    if (it == modules.end()) {
      return false;
    }
    modules.erase(it);
    OVERNET_TRACE(INFO) << "Using module " << name;
    return true;
  }
};

// Processors figure out what the command line means.
// One option processor here for each legal command line option.
// The processor returns true if the option is legal, or prints a message to
// std::cerr and returns false if the option is invalid.
static const auto kConfigurationCommandLineProcessors = []() {
  std::map<std::string,
           std::function<bool(Configuration * config, const std::string&)>>
      processors;
  processors["help"] = [](Configuration* config, const std::string& value) {
    config->help = true;
    return true;
  };
  processors["verbosity"] = [](Configuration* config,
                               const std::string& value) {
    if (value.empty()) {
      config->trace_severity = overnet::Severity::INFO;
      return true;
    }
    if (auto severity = overnet::SeverityFromString(value);
        severity.has_value()) {
      config->trace_severity = *severity;
      return true;
    }
    std::cerr << "Unknown trace severity: " << value << "\n";
    return false;
  };
  processors["modules"] = [](Configuration* config, const std::string& value) {
    config->modules.clear();
    std::string::size_type prev_pos = 0;
    std::string::size_type pos;
    while ((pos = value.find(',', prev_pos)) != std::string::npos) {
      config->modules.emplace(Trim(value.substr(prev_pos, pos - prev_pos)));
      prev_pos = pos + 1;
    }
    config->modules.emplace(Trim(value.substr(prev_pos)));
    return true;
  };
  return processors;
}();

bool Configuration::ParseCommandLine(int argc, const char** argv) {
  auto cmdline = fxl::CommandLineFromArgcArgv(argc, argv);
  for (const auto& option : cmdline.options()) {
    auto processor = kConfigurationCommandLineProcessors.find(option.name);
    if (processor == kConfigurationCommandLineProcessors.end()) {
      std::cerr << "Unknown option: " << option.name << "\n";
      return false;
    }
    if (!processor->second(this, option.value)) {
      return false;
    }
  }
  if (!cmdline.positional_args().empty()) {
    std::cerr << "Expected no positional args\n";
    return false;
  }
  return true;
}

void Usage() {
  Configuration default_config;
  std::cout
      << "overnetstack - Runs Overnet and associated protocols\n"
      << "Arguments:\n"
      << "  --help          display this help and exit\n"
      << "  --verbosity=X   set the log verbosity (X is one of: debug, trace,\n"
      << "                  info, warning, error)\n"
      << "                  defaults to: " << default_config.trace_severity
      << "\n"
      << "  --modules=X     comma separated list of modules to start, where\n"
      << "                  X could be:\n"
      << "                  udp               allow communication over udp\n"
      << "                  mdns_advertiser   advertise udp communications\n"
      << "                                    over mdns\n"
      << "                  mdns_subscriber   listen for udp communications\n"
      << "                                    with mdns\n"
      << "                  omdp              advertise *and* listen for udp\n"
      << "                                    communications using omdp\n"
      << "                  defaults to: ";
  bool first = true;
  for (const auto& module : default_config.modules) {
    if (!first) {
      std::cout << ",";
    }
    first = false;
    std::cout << module;
  }
  std::cout << "\n";
}

}  // namespace overnetstack

int main(int argc, const char** argv) {
  // Parse command line
  auto config = std::make_unique<overnetstack::Configuration>();
  if (!config->ParseCommandLine(argc, argv)) {
    overnetstack::Usage();
    return 1;
  } else if (config->help) {
    overnetstack::Usage();
    return 0;
  }
  // Configure fxl to just print what we tell it to (overnet tracing will deal
  // with actual log levels)
  {
    auto settings = fxl::GetLogSettings();
    settings.min_log_level = -1;
    fxl::SetLogSettings(settings);
  }
  // Wire up the application objects.
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  overnetstack::FuchsiaTimer fuchsia_timer;
  overnetstack::FuchsiaLog fuchsia_log(&fuchsia_timer);
  overnet::ScopedRenderer scoped_renderer(&fuchsia_log);
  overnet::ScopedSeverity scoped_severity(config->trace_severity);
  overnetstack::OvernetApp app(&fuchsia_timer);
  app.InstantiateActor<overnetstack::Service>();
  overnetstack::UdpNub* udp_nub = nullptr;
  // Build up actors for the configuration.
  for (auto module : config->modules) {
    OVERNET_TRACE(INFO) << "GOT MODULE: " << module;
  }
  if (config->TakeModule("udp")) {
    udp_nub = app.InstantiateActor<overnetstack::UdpNub>();
  }
  if (config->TakeModule("mdns_advertiser")) {
    if (udp_nub == nullptr) {
      std::cerr << "mdns_advertiser requires udp\n";
      return 1;
    }
    app.InstantiateActor<overnetstack::MdnsAdvertisement>(udp_nub);
  }
  if (config->TakeModule("mdns_subscriber")) {
    if (udp_nub == nullptr) {
      std::cerr << "mdns_subscriber requires udp\n";
      return 1;
    }
    app.InstantiateActor<overnetstack::MdnsIntroducer>(udp_nub);
  }
  if (config->TakeModule("omdp")) {
    if (udp_nub == nullptr) {
      std::cerr << "omdp requires udp\n";
      return 1;
    }
    app.InstantiateActor<overnetstack::OmdpNub>(udp_nub);
  }
  // Remaining modules we didn't know about: complain and exit
  if (!config->modules.empty()) {
    for (const auto& module : config->modules) {
      std::cerr << "Unknown module: " << module << "\n";
    }
    return 1;
  }
  // Throw away the configuration data: it's no longer necessary
  config.reset();
  // Run the main loop
  auto status = app.Start().Then([&]() {
    switch (auto status = loop.Run()) {
      case ZX_OK:
      case ZX_ERR_CANCELED:
        return overnet::Status::Ok();
      default:
        return overnet::Status::FromZx(status).WithContext("RunLoop");
    }
  });
  if (status.is_ok()) {
    return 0;
  } else {
    OVERNET_TRACE(ERROR) << "Failed to start overnetstack: " << status << "\n";
    return static_cast<int>(status.code());
  }
}
