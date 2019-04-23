// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fdio/watcher.h>
#include <lib/fit/promise.h>
#include <lib/fit/sequencer.h>
#include <lib/fsl/io/fd.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/termination_reason.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/pkg_url/fuchsia_pkg_url.h>
#include <zircon/status.h>

#include "garnet/lib/cmx/cmx.h"

using namespace fuchsia::netemul;

namespace netemul {

static const char* kEndpointMountPath = "class/ethernet/";
constexpr int64_t kFailureTerminationCode = -1;
constexpr int64_t kTimeoutTerminationCode = 1;

#define STATIC_MSG_STRUCT(name, msgv) \
  struct name {                       \
    static const char* msg;           \
  };                                  \
  const char* name::msg = msgv;

STATIC_MSG_STRUCT(kMsgApp, "app");
STATIC_MSG_STRUCT(kMsgTest, "test");

// Sandbox uses two threads to operate:
// a main thread (which it's initialized with)
// + a helper thread.
// The macros below are used to assert that methods on
// the sandbox class are called on the proper thread
#define ASSERT_DISPATCHER(disp) \
  ZX_ASSERT((disp) == async_get_default_dispatcher())
#define ASSERT_MAIN_DISPATCHER ASSERT_DISPATCHER(main_dispatcher_)
#define ASSERT_HELPER_DISPATCHER ASSERT_DISPATCHER(helper_loop_->dispatcher())

Sandbox::Sandbox(SandboxArgs args) : env_config_(std::move(args.config)) {
  auto services = sys::ServiceDirectory::CreateFromNamespace();
  services->Connect(parent_env_.NewRequest());
  services->Connect(loader_.NewRequest());
  parent_env_.set_error_handler([](zx_status_t err) {
    FXL_LOG(ERROR) << "Lost connection to parent environment";
  });
}

void Sandbox::Start(async_dispatcher_t* dispatcher) {
  main_dispatcher_ = dispatcher;
  setup_done_ = false;
  test_spawned_ = false;

  if (!parent_env_ || !loader_) {
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  } else if (env_config_.disabled()) {
    FXL_LOG(INFO) << "test is disabled, skipping.";
    Terminate(0, TerminationReason::EXITED);
    return;
  }

  helper_loop_ =
      std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
  if (helper_loop_->StartThread("helper-thread") != ZX_OK) {
    FXL_LOG(ERROR) << "Can't start config thread";
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  }
  helper_executor_ =
      std::make_unique<async::Executor>(helper_loop_->dispatcher());

  SandboxEnv::Events global_events;
  global_events.service_terminated = [this](const std::string& service,
                                            int64_t exit_code,
                                            TerminationReason reason) {
    if (helper_loop_ &&
        (reason != TerminationReason::EXITED || exit_code != 0)) {
      async::PostTask(helper_loop_->dispatcher(), [this]() {
        PostTerminate(TerminationReason::INTERNAL_ERROR);
      });
    }
  };
  sandbox_env_ = std::make_shared<SandboxEnv>(std::move(global_events));
  sandbox_env_->set_default_name(env_config_.default_url());

  if (services_created_callback_) {
    services_created_callback_();
  }

  StartEnvironments();
}

void Sandbox::Terminate(int64_t exit_code, Sandbox::TerminationReason reason) {
  // all processes must have been emptied to call callback
  ASSERT_MAIN_DISPATCHER;
  ZX_ASSERT(procs_.empty());

  if (helper_loop_) {
    helper_loop_->Quit();
    helper_loop_->JoinThreads();
    helper_loop_ = nullptr;
  }

  if (exit_code != 0 || env_config_.capture() == config::CaptureMode::ALWAYS) {
    // check if any of the network dumps have data, and just dump them to
    // stdout:
    if (net_dumps_ && net_dumps_->HasData()) {
      std::cout << "PCAP dump for all network data ==================="
                << std::endl;
      net_dumps_->dump().DumpHex(&std::cout);
      std::cout << "================================================"
                << std::endl;
    }
  }

  if (termination_callback_) {
    termination_callback_(exit_code, reason);
  }
}

void Sandbox::PostTerminate(TerminationReason reason) {
  ASSERT_HELPER_DISPATCHER;
  PostTerminate(kFailureTerminationCode, reason);
}

void Sandbox::PostTerminate(int64_t exit_code, TerminationReason reason) {
  ASSERT_HELPER_DISPATCHER;
  // kill all component controllers before posting termination
  procs_.clear();
  async::PostTask(main_dispatcher_, [this, exit_code, reason]() {
    Terminate(exit_code, reason);
  });
}

void Sandbox::Terminate(Sandbox::TerminationReason reason) {
  ASSERT_MAIN_DISPATCHER;
  Terminate(kFailureTerminationCode, reason);
}

void Sandbox::StartEnvironments() {
  ASSERT_MAIN_DISPATCHER;

  async::PostTask(helper_loop_->dispatcher(), [this]() {
    if (!ConfigureNetworks()) {
      PostTerminate(TerminationReason::INTERNAL_ERROR);
      return;
    }

    ManagedEnvironment::Options root_options;
    if (!CreateEnvironmentOptions(env_config_.environment(), &root_options)) {
      PostTerminate(TerminationReason::INTERNAL_ERROR);
      return;
    }

    async::PostTask(main_dispatcher_,
                    [this, root_options = std::move(root_options)]() mutable {
                      ASSERT_MAIN_DISPATCHER;
                      root_ = ManagedEnvironment::CreateRoot(
                          parent_env_, sandbox_env_, std::move(root_options));
                      root_->SetRunningCallback([this]() {
                        if (root_environment_created_callback_) {
                          root_environment_created_callback_(root_.get());
                        }

                        // configure root environment:
                        async::PostTask(helper_loop_->dispatcher(), [this]() {
                          ConfigureRootEnvironment();
                        });
                      });
                    });
  });
}

// configure networks runs in an auxiliary thread, so we can use
// synchronous calls to the fidl service
bool Sandbox::ConfigureNetworks() {
  ASSERT_HELPER_DISPATCHER;
  // start by configuring the networks:

  if (env_config_.networks().empty()) {
    return true;
  }

  network::NetworkContextSyncPtr net_ctx;

  auto req = net_ctx.NewRequest();

  // bind to network context
  async::PostTask(main_dispatcher_, [req = std::move(req), this]() mutable {
    sandbox_env_->network_context().GetHandler()(std::move(req));
  });

  network::NetworkManagerSyncPtr net_manager;
  network::EndpointManagerSyncPtr endp_manager;
  net_ctx->GetNetworkManager(net_manager.NewRequest());
  net_ctx->GetEndpointManager(endp_manager.NewRequest());

  for (const auto& net_cfg : env_config_.networks()) {
    zx_status_t status;
    fidl::InterfaceHandle<network::Network> network_h;
    if (net_manager->CreateNetwork(net_cfg.name(), network::NetworkConfig(),
                                   &status, &network_h) != ZX_OK ||
        status != ZX_OK) {
      FXL_LOG(ERROR) << "Create network failed";
      return false;
    }

    auto network = network_h.BindSync();

    if (env_config_.capture() != config::CaptureMode::NONE) {
      if (!net_dumps_) {
        net_dumps_ = std::make_unique<NetWatcher<InMemoryDump>>();
      }
      fidl::InterfacePtr<network::FakeEndpoint> fake_endpoint;
      network->CreateFakeEndpoint(fake_endpoint.NewRequest());
      net_dumps_->Watch(net_cfg.name(), std::move(fake_endpoint));
    }

    for (const auto& endp_cfg : net_cfg.endpoints()) {
      network::EndpointConfig fidl_config;
      fidl::InterfaceHandle<network::Endpoint> endp_h;

      fidl_config.backing = network::EndpointBacking::ETHERTAP;
      fidl_config.mtu = endp_cfg.mtu();
      if (endp_cfg.mac()) {
        fidl_config.mac =
            std::make_unique<fuchsia::hardware::ethernet::MacAddress>();
        memcpy(&fidl_config.mac->octets[0], endp_cfg.mac()->d, 6);
      }

      if (endp_manager->CreateEndpoint(endp_cfg.name(), std::move(fidl_config),
                                       &status, &endp_h) != ZX_OK ||
          status != ZX_OK) {
        FXL_LOG(ERROR) << "Create endpoint failed";
        return false;
      }

      auto endp = endp_h.BindSync();

      if (endp_cfg.up()) {
        if (endp->SetLinkUp(true) != ZX_OK) {
          FXL_LOG(ERROR) << "Set endpoint up failed";
          return false;
        }
      }

      // add endpoint to network:
      if (network->AttachEndpoint(endp_cfg.name(), &status) != ZX_OK ||
          status != ZX_OK) {
        FXL_LOG(ERROR) << "Attaching endpoint " << endp_cfg.name()
                       << " to network " << net_cfg.name() << " failed";
        return false;
      }

      // save the endpoint handle:
      network_handles_.emplace_back(endp.Unbind().TakeChannel());
    }

    // save the network handle:
    network_handles_.emplace_back(network.Unbind().TakeChannel());
  }

  return true;
}

// Create environment options runs in an auxiliary thread, so we can use
// synchronous calls to fidl services
bool Sandbox::CreateEnvironmentOptions(const config::Environment& config,
                                       ManagedEnvironment::Options* options) {
  ASSERT_HELPER_DISPATCHER;
  options->set_name(config.name());
  options->set_inherit_parent_launch_services(config.inherit_services());

  std::vector<environment::VirtualDevice>* devices = options->mutable_devices();
  if (!config.devices().empty()) {
    network::EndpointManagerSyncPtr epm;
    async::PostTask(main_dispatcher_, [req = epm.NewRequest(), this]() mutable {
      sandbox_env_->network_context().endpoint_manager().Bind(std::move(req));
    });
    for (const auto& device : config.devices()) {
      auto& nd = devices->emplace_back();
      nd.path = fxl::Concatenate({std::string(kEndpointMountPath), device});

      fidl::InterfaceHandle<network::Endpoint> endp_h;
      if (epm->GetEndpoint(device, &endp_h) != ZX_OK) {
        FXL_LOG(ERROR) << "Can't find endpoint " << device
                       << " on endpoint manager";
        return false;
      }

      auto endp = endp_h.BindSync();
      if (endp->GetProxy(nd.device.NewRequest()) != ZX_OK) {
        FXL_LOG(ERROR) << "Can't get proxy on endpoint " << device;
        return false;
      }
    }
  }

  std::vector<environment::LaunchService>* services =
      options->mutable_services();
  for (const auto& svc : config.services()) {
    auto& ns = services->emplace_back();
    ns.name = svc.name();
    ns.url = svc.launch().GetUrlOrDefault(sandbox_env_->default_name());
    ns.arguments->insert(ns.arguments->end(), svc.launch().arguments().begin(),
                         svc.launch().arguments().end());
  }

  // Logger options
  fuchsia::netemul::environment::LoggerOptions* logger_options =
      options->mutable_logger_options();
  const config::LoggerOptions& config_logger_options = config.logger_options();
  logger_options->set_enabled(config_logger_options.enabled());
  logger_options->set_klogs_enabled(config_logger_options.klogs_enabled());

  fuchsia::logger::LogFilterOptions* log_filter_options =
      logger_options->mutable_filter_options();
  const config::LoggerFilterOptions& config_logger_filter_options =
      config_logger_options.filters();
  log_filter_options->verbosity = config_logger_filter_options.verbosity();
  log_filter_options->tags = config_logger_filter_options.tags();

  return true;
}

void Sandbox::ConfigureRootEnvironment() {
  ASSERT_HELPER_DISPATCHER;
  // connect to environment:
  auto svc = std::make_shared<environment::ManagedEnvironmentSyncPtr>();
  auto req = svc->NewRequest();

  async::PostTask(main_dispatcher_, [this, req = std::move(req)]() mutable {
    root_->Bind(std::move(req));
  });

  fit::schedule_for_consumer(
      helper_executor_.get(),
      ConfigureEnvironment(std::move(svc), &env_config_.environment(), true)
          .or_else(
              [this]() { PostTerminate(TerminationReason::INTERNAL_ERROR); }));
}

fit::promise<> Sandbox::StartChildEnvironment(
    ConfiguringEnvironmentPtr parent, const config::Environment* config) {
  ASSERT_HELPER_DISPATCHER;

  return fit::make_promise(
             [this, parent,
              config]() -> fit::result<ConfiguringEnvironmentPtr> {
               ManagedEnvironment::Options options;
               if (!CreateEnvironmentOptions(*config, &options)) {
                 return fit::error();
               }
               auto child_env =
                   std::make_shared<environment::ManagedEnvironmentSyncPtr>();
               if ((*parent)->CreateChildEnvironment(
                       child_env->NewRequest(), std::move(options)) != ZX_OK) {
                 return fit::error();
               }

               return fit::ok(std::move(child_env));
             })
      .and_then([this, config](ConfiguringEnvironmentPtr& child_env) {
        return ConfigureEnvironment(std::move(child_env), config);
      });
}

fit::promise<> Sandbox::StartEnvironmentSetup(
    const config::Environment* config,
    ConfiguringEnvironmentLauncher launcher) {
  return fit::make_promise([this, config, launcher = std::move(launcher)] {
    auto prom = fit::make_ok_promise().box();
    for (const auto& setup : config->setup()) {
      prom = prom.and_then([this, setup = &setup, launcher]() {
                   return LaunchSetup(
                       launcher.get(),
                       setup->GetUrlOrDefault(sandbox_env_->default_name()),
                       setup->arguments());
                 })
                 .box();
    }
    return prom;
  });
}

fit::promise<> Sandbox::StartEnvironmentAppsAndTests(
    const netemul::config::Environment* config,
    netemul::Sandbox::ConfiguringEnvironmentLauncher launcher) {
  return fit::make_promise([this, config,
                            launcher = std::move(launcher)]() -> fit::result<> {
    for (const auto& app : config->apps()) {
      if (!LaunchProcess<kMsgApp>(
              launcher.get(), app.GetUrlOrDefault(sandbox_env_->default_name()),
              app.arguments(), false)) {
        return fit::error();
      }
    }

    for (const auto& test : config->test()) {
      if (!LaunchProcess<kMsgTest>(
              launcher.get(),
              test.GetUrlOrDefault(sandbox_env_->default_name()),
              test.arguments(), true)) {
        return fit::error();
      }
      // save that at least one test was spawned.
      test_spawned_ = true;
    }

    return fit::ok();
  });
}

fit::promise<> Sandbox::StartEnvironmentInner(
    ConfiguringEnvironmentPtr env, const config::Environment* config) {
  ASSERT_HELPER_DISPATCHER;
  auto launcher = std::make_shared<fuchsia::sys::LauncherSyncPtr>();
  return fit::make_promise([this, launcher, env]() -> fit::result<> {
           // get launcher
           if ((*env)->GetLauncher(launcher->NewRequest()) != ZX_OK) {
             FXL_LOG(ERROR) << "Can't get environment launcher";
             return fit::error();
           }
           return fit::ok();
         })
      .and_then(StartEnvironmentSetup(config, launcher))
      .and_then(StartEnvironmentAppsAndTests(config, launcher));
}

fit::promise<> Sandbox::ConfigureEnvironment(ConfiguringEnvironmentPtr env,
                                             const config::Environment* config,
                                             bool root) {
  ASSERT_HELPER_DISPATCHER;

  std::vector<fit::promise<>> promises;

  // iterate on children
  for (const auto& child : config->children()) {
    // start each one of the child environments
    promises.emplace_back(StartChildEnvironment(env, &child));
  }

  // start this processes inside this environment
  auto self_start = StartEnvironmentInner(env, config);
  if (root) {
    // if root, after everything is set up, enable observing
    // test returns.
    promises.emplace_back(
        self_start.and_then([this]() { EnableTestObservation(); }));
  } else {
    promises.emplace_back(std::move(self_start));
  }

  return fit::join_promise_vector(std::move(promises))
      .and_then([](std::vector<fit::result<>>& results) -> fit::result<> {
        for (const auto& r : results) {
          if (r.is_error()) {
            return fit::error();
          }
        }
        return fit::ok();
      });
}

template <typename T>
bool Sandbox::LaunchProcess(fuchsia::sys::LauncherSyncPtr* launcher,
                            const std::string& url,
                            const std::vector<std::string>& arguments,
                            bool is_test) {
  ASSERT_HELPER_DISPATCHER;

  fuchsia::sys::LaunchInfo linfo;
  linfo.url = url;
  linfo.arguments->insert(linfo.arguments->end(), arguments.begin(),
                          arguments.end());

  auto ticket = procs_.size();
  auto& proc = procs_.emplace_back();

  if (is_test) {
    RegisterTest(ticket);
  }

  proc.set_error_handler([this](zx_status_t status) {
    PostTerminate(TerminationReason::INTERNAL_ERROR);
  });

  // we observe test processes return code
  proc.events().OnTerminated = [url, this, is_test, ticket](
                                   int64_t code, TerminationReason reason) {
    FXL_LOG(INFO) << T::msg << " " << url << " terminated with (" << code
                  << ") reason: "
                  << sys::HumanReadableTerminationReason(reason);
    // remove the error handler:
    procs_[ticket].set_error_handler(nullptr);
    if (is_test) {
      if (code != 0 || reason != TerminationReason::EXITED) {
        // test failed, early bail
        PostTerminate(code, reason);
      } else {
        // unregister test ticket
        UnregisterTest(ticket);
      }
    }
  };

  if ((*launcher)->CreateComponent(std::move(linfo), proc.NewRequest()) !=
      ZX_OK) {
    FXL_LOG(ERROR) << "couldn't launch " << T::msg << ": " << url;
    return false;
  }

  return true;
}

fit::promise<> Sandbox::LaunchSetup(fuchsia::sys::LauncherSyncPtr* launcher,
                                    const std::string& url,
                                    const std::vector<std::string>& arguments) {
  ASSERT_HELPER_DISPATCHER;

  fit::bridge<> bridge;

  fuchsia::sys::LaunchInfo linfo;
  linfo.url = url;
  linfo.arguments->insert(linfo.arguments->end(), arguments.begin(),
                          arguments.end());

  auto ticket = procs_.size();
  auto& proc = procs_.emplace_back();

  if ((*launcher)->CreateComponent(std::move(linfo), proc.NewRequest()) !=
      ZX_OK) {
    FXL_LOG(ERROR) << "couldn't launch setup: " << url;
    bridge.completer.complete_error();
  } else {
    proc.set_error_handler([this](zx_status_t status) {
      PostTerminate(TerminationReason::INTERNAL_ERROR);
    });

    // we observe test processes return code
    proc.events().OnTerminated =
        [url, this, ticket, completer = std::move(bridge.completer)](
            int64_t code, TerminationReason reason) mutable {
          FXL_LOG(INFO) << "Setup " << url << " terminated with (" << code
                        << ") reason: "
                        << sys::HumanReadableTerminationReason(reason);
          // remove the error handler:
          procs_[ticket].set_error_handler(nullptr);
          if (code == 0 && reason == TerminationReason::EXITED) {
            completer.complete_ok();
          } else {
            completer.complete_error();
          }
        };
  }

  return bridge.consumer.promise();
}

void Sandbox::EnableTestObservation() {
  ASSERT_HELPER_DISPATCHER;

  setup_done_ = true;

  // if we're not observing any tests,
  // consider it a failure.
  if (!test_spawned_) {
    FXL_LOG(ERROR) << "No tests were specified";
    PostTerminate(TerminationReason::INTERNAL_ERROR);
    return;
  }

  if (tests_.empty()) {
    // all tests finished successfully
    PostTerminate(0, TerminationReason::EXITED);
    return;
  }

  // if a timeout is specified, start counting it from now:
  if (env_config_.timeout() != zx::duration::infinite()) {
    async::PostDelayedTask(
        helper_loop_->dispatcher(),
        [this]() {
          FXL_LOG(ERROR) << "Test timed out.";
          PostTerminate(kTimeoutTerminationCode, TerminationReason::EXITED);
        },
        env_config_.timeout());
  }
}

void Sandbox::RegisterTest(size_t ticket) {
  ASSERT_HELPER_DISPATCHER;

  tests_.insert(ticket);
}

void Sandbox::UnregisterTest(size_t ticket) {
  ASSERT_HELPER_DISPATCHER;

  tests_.erase(ticket);
  if (setup_done_ && tests_.empty()) {
    // all tests finished successfully
    PostTerminate(0, TerminationReason::EXITED);
  }
}

bool SandboxArgs::ParseFromJSON(const rapidjson::Value& facet,
                                json::JSONParser* json_parser) {
  if (!config.ParseFromJSON(facet, json_parser)) {
    FXL_LOG(ERROR) << "netemul facet failed to parse: "
                   << json_parser->error_str();
    return false;
  }
  return true;
}

bool SandboxArgs::ParseFromString(const std::string& config) {
  json::JSONParser json_parser;
  auto facet = json_parser.ParseFromString(config, "fuchsia.netemul facet");
  if (json_parser.HasError()) {
    FXL_LOG(ERROR) << "netemul facet failed to parse: "
                   << json_parser.error_str();
    return false;
  }

  return ParseFromJSON(facet, &json_parser);
}

bool SandboxArgs::ParseFromCmxFileAt(int dir, const std::string& path) {
  component::CmxMetadata cmx;
  json::JSONParser json_parser;
  if (!cmx.ParseFromFileAt(dir, path, &json_parser)) {
    FXL_LOG(ERROR) << "cmx file failed to parse: " << json_parser.error_str();
    return false;
  }

  return ParseFromJSON(cmx.GetFacet(config::Config::Facet), &json_parser);
}

}  // namespace netemul
