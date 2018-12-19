// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox.h"
#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/component/cpp/termination_reason.h>
#include <lib/component/cpp/testing/test_util.h>
#include <lib/fdio/watcher.h>
#include <lib/fsl/io/fd.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/pkg_url/fuchsia_pkg_url.h>
#include <zircon/status.h>
#include "garnet/lib/cmx/cmx.h"

using namespace fuchsia::netemul;

namespace netemul {

static const char* kEndpointMountPath = "class/ethernet/";
// give setup processes a maximum of 10s before bailing
static const int kSetupTimeoutSecs = 10;

#define STATIC_MSG_STRUCT(name, msgv) \
  struct name {                       \
    static const char* msg;           \
  };                                  \
  const char* name::msg = msgv;

STATIC_MSG_STRUCT(kMsgApp, "app");
STATIC_MSG_STRUCT(kMsgTest, "test");
STATIC_MSG_STRUCT(kMsgRoot, "root test");

// Sandbox uses two threads to operate:
// a main thread (which it's initialized with)
// + a helper thread.
// The macros below are used to assert that methods on
// the sandbox class are called on the proper thread
#define ASSERT_DISPATCHER(disp) \
  ZX_ASSERT((disp) == async_get_default_dispatcher())
#define ASSERT_MAIN_DISPATCHER ASSERT_DISPATCHER(main_dispatcher_)
#define ASSERT_HELPER_DISPATCHER ASSERT_DISPATCHER(helper_loop_->dispatcher())

Sandbox::Sandbox(SandboxArgs args) : args_(std::move(args)) {
  auto startup_context = component::StartupContext::CreateFromStartupInfo();
  startup_context->ConnectToEnvironmentService(parent_env_.NewRequest());
  startup_context->ConnectToEnvironmentService(loader_.NewRequest());
  parent_env_.set_error_handler([](zx_status_t err) {
    FXL_LOG(ERROR) << "Lost connection to parent environment";
  });
}

void Sandbox::Start(async_dispatcher_t* dispatcher) {
  if (!parent_env_ || !loader_) {
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  }

  main_dispatcher_ = dispatcher;
  setup_done_ = false;

  helper_loop_ =
      std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
  if (helper_loop_->StartThread("helper-thread") != ZX_OK) {
    FXL_LOG(ERROR) << "Can't start config thread";
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  }

  loader_->LoadUrl(args_.package, [this](fuchsia::sys::PackagePtr package) {
    if (!package) {
      Terminate(TerminationReason::PACKAGE_NOT_FOUND);
      return;
    } else if (!package->directory) {
      Terminate(TerminationReason::INTERNAL_ERROR);
    }
    LoadPackage(std::move(package));
  });
}

void Sandbox::Terminate(int64_t exit_code, Sandbox::TerminationReason reason) {
  // all processes must have been emptied to call callback
  ASSERT_MAIN_DISPATCHER;
  ZX_ASSERT(procs_.empty());
  if (termination_callback_) {
    termination_callback_(exit_code, reason);
  }
}

void Sandbox::PostTerminate(TerminationReason reason) {
  ASSERT_HELPER_DISPATCHER;
  PostTerminate(-1, reason);
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
  Terminate(-1, reason);
}

void Sandbox::LoadPackage(fuchsia::sys::PackagePtr package) {
  ASSERT_MAIN_DISPATCHER;
  // package is loaded, proceed to parsing cmx and starting child env
  component::FuchsiaPkgUrl pkgUrl;
  if (!pkgUrl.Parse(package->resolved_url)) {
    FXL_LOG(ERROR) << "Can't parse fuchsia url: " << package->resolved_url;
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  }

  fxl::UniqueFD dirfd =
      fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

  sandbox_env_ = std::make_shared<SandboxEnv>(args_.package, std::move(dirfd));

  component::CmxMetadata cmx;

  json::JSONParser json_parser;
  if (!cmx.ParseFromFileAt(sandbox_env_->dir().get(), pkgUrl.resource_path(),
                           &json_parser)) {
    FXL_LOG(ERROR) << "cmx file failed to parse: " << json_parser.error_str();
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  }

  if (!env_config_.ParseFromJSON(cmx.GetFacet(config::Config::Facet),
                                 &json_parser)) {
    FXL_LOG(ERROR) << "netemul facet failed to parse: "
                   << json_parser.error_str();
    Terminate(TerminationReason::INTERNAL_ERROR);
    return;
  }

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
                        // configure root environment:
                        async::PostTask(helper_loop_->dispatcher(), [this]() {
                          if (!ConfigureRootEnvironment()) {
                            PostTerminate(TerminationReason::INTERNAL_ERROR);
                          }
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
  options->name = config.name();
  options->inherit_parent_launch_services = config.inherit_services();

  std::vector<environment::VirtualDevice>& devices = options->devices;
  ;
  if (!config.devices().empty()) {
    network::EndpointManagerSyncPtr epm;
    async::PostTask(main_dispatcher_, [req = epm.NewRequest(), this]() mutable {
      sandbox_env_->network_context().endpoint_manager().Bind(std::move(req));
    });
    for (const auto& device : config.devices()) {
      auto& nd = devices.emplace_back();
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

  std::vector<environment::LaunchService>& services = options->services;
  for (const auto& svc : config.services()) {
    auto& ns = services.emplace_back();
    ns.name = svc.name();
    ns.url = svc.launch().GetUrlOrDefault(sandbox_env_->name());
    ns.arguments->insert(ns.arguments->end(), svc.launch().arguments().begin(),
                         svc.launch().arguments().end());
  }

  return true;
}

bool Sandbox::ConfigureRootEnvironment() {
  ASSERT_HELPER_DISPATCHER;
  // connect to environment:
  environment::ManagedEnvironmentSyncPtr svc;
  auto req = svc.NewRequest();

  async::PostTask(main_dispatcher_, [this, req = std::move(req)]() mutable {
    root_->Bind(std::move(req));
  });

  // configure with service pointer:
  return ConfigureEnvironment(std::move(svc), env_config_.environment(), true);
}

// Configure environment runs in an auxiliary thread, so we can use synchronous
// calls to fidl services
bool Sandbox::ConfigureEnvironment(
    fidl::SynchronousInterfacePtr<ManagedEnvironment::FManagedEnvironment> env,
    const config::Environment& config, bool root) {
  ASSERT_HELPER_DISPATCHER;
  // iterate on children
  for (const auto& child : config.children()) {
    ManagedEnvironment::Options options;
    if (!CreateEnvironmentOptions(child, &options)) {
      return false;
    }
    environment::ManagedEnvironmentSyncPtr child_env;
    if (env->CreateChildEnvironment(child_env.NewRequest(),
                                    std::move(options)) != ZX_OK) {
      FXL_LOG(ERROR) << "Creating environment \"" << child.name()
                     << "\" failed";
      return false;
    }

    // child environment was successfully created, configure child:
    if (!ConfigureEnvironment(std::move(child_env), child)) {
      return false;
    }
  }

  // get launcher
  fuchsia::sys::LauncherSyncPtr launcher;
  if (env->GetLauncher(launcher.NewRequest()) != ZX_OK) {
    FXL_LOG(ERROR) << "Can't get environment launcher";
    return false;
  }

  for (const auto& app : config.apps()) {
    if (!LaunchProcess<kMsgApp>(&launcher,
                                app.GetUrlOrDefault(sandbox_env_->name()),
                                app.arguments(), false)) {
      return false;
    }
  }

  for (const auto& setup : config.setup()) {
    if (!LaunchSetup(&launcher, setup.GetUrlOrDefault(sandbox_env_->name()),
                     setup.arguments())) {
      return false;
    }
  }

  for (const auto& test : config.test()) {
    if (!LaunchProcess<kMsgTest>(&launcher,
                                 test.GetUrlOrDefault(sandbox_env_->name()),
                                 test.arguments(), true)) {
      return false;
    }
  }

  if (root) {
    if (!LaunchProcess<kMsgRoot>(&launcher, sandbox_env_->name(), args_.args,
                                 true)) {
      return false;
    }
    EnableTestObservation();
  }

  return true;
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
  linfo.out = component::testing::CloneFileDescriptor(STDOUT_FILENO);
  linfo.err = component::testing::CloneFileDescriptor(STDERR_FILENO);

  auto& proc = procs_.emplace_back();
  auto ticket = procs_.size();

  if (is_test) {
    RegisterTest(ticket);
  }

  // we observe test processes return code
  proc.events().OnTerminated = [url, this, is_test, ticket](
                                   int64_t code, TerminationReason reason) {
    FXL_LOG(INFO) << T::msg << " " << url << " terminated with (" << code
                  << ") reason: "
                  << component::HumanReadableTerminationReason(reason);
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

bool Sandbox::LaunchSetup(fuchsia::sys::LauncherSyncPtr* launcher,
                          const std::string& url,
                          const std::vector<std::string>& arguments) {
  ASSERT_HELPER_DISPATCHER;

  fuchsia::sys::LaunchInfo linfo;
  linfo.url = url;
  linfo.arguments->insert(linfo.arguments->end(), arguments.begin(),
                          arguments.end());
  linfo.out = component::testing::CloneFileDescriptor(STDOUT_FILENO);
  linfo.err = component::testing::CloneFileDescriptor(STDERR_FILENO);

  fuchsia::sys::ComponentControllerPtr proc;
  bool done = false;
  bool success = false;

  // we observe test processes return code
  proc.events().OnTerminated = [url, this, &done, &success](
                                   int64_t code, TerminationReason reason) {
    FXL_LOG(INFO) << "Setup " << url << " terminated with (" << code
                  << ") reason: "
                  << component::HumanReadableTerminationReason(reason);
    done = true;
    success = code == 0 && reason == TerminationReason::EXITED;
  };

  if ((*launcher)->CreateComponent(std::move(linfo), proc.NewRequest()) !=
      ZX_OK) {
    FXL_LOG(ERROR) << "couldn't launch setup: " << url;
    return false;
  }

  while (!done) {
    auto status =
        helper_loop_->Run(zx::deadline_after(zx::sec(kSetupTimeoutSecs)), true);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Setup " << url << " run loop exited with: "
                     << zx_status_get_string(status);
      return false;
    }
  }

  return success;
}

void Sandbox::EnableTestObservation() {
  ASSERT_HELPER_DISPATCHER;

  setup_done_ = true;
  if (tests_.empty()) {
    // all tests finished successfully
    PostTerminate(0, TerminationReason::EXITED);
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

}  // namespace netemul
