// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/startup_agent_launcher.h"

#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/svc/cpp/service_namespace.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/files/file.h"
#include "src/modular/lib/connect/connect.h"

namespace modular {

namespace {

static constexpr modular::RateLimitedRetry::Threshold kSessionAgentRetryLimit = {3, zx::sec(45)};

static constexpr char kInternalAgentRunnerRequestorUrl[] = "builtin://modular";

}  // namespace

template <class Interface>
StartupAgentLauncher::SessionAgentData::DeferredInterfaceRequest::DeferredInterfaceRequest(
    fidl::InterfaceRequest<Interface> request)
    : name(Interface::Name_), channel(request.TakeChannel()) {}

StartupAgentLauncher::SessionAgentData::SessionAgentData() : restart(kSessionAgentRetryLimit) {}

template <class Interface>
void StartupAgentLauncher::SessionAgentData::ConnectOrQueueServiceRequest(
    fidl::InterfaceRequest<Interface> request) {
  if (services) {
    connect::ConnectToService(services.get(), std::move(request));
  } else {
    pending_service_requests.emplace_back(std::move(request));
  }
}

StartupAgentLauncher::StartupAgentLauncher(
    fidl::InterfaceRequestHandler<fuchsia::modular::PuppetMaster> puppet_master_connector,
    fidl::InterfaceRequestHandler<fuchsia::modular::SessionRestartController>
        session_restart_controller_connector,
    fidl::InterfaceRequestHandler<fuchsia::intl::PropertyProvider> intl_property_provider_connector,
    fuchsia::sys::ServiceList additional_services_for_agents,
    fit::function<bool()> is_terminating_cb)
    : puppet_master_connector_(std::move(puppet_master_connector)),
      session_restart_controller_connector_(std::move(session_restart_controller_connector)),
      intl_property_provider_connector_(std::move(intl_property_provider_connector)),
      additional_services_for_agents_(std::move(additional_services_for_agents)),
      additional_services_for_agents_directory_(
          std::move(additional_services_for_agents_.host_directory)),
      is_terminating_cb_(std::move(is_terminating_cb)) {}

void StartupAgentLauncher::StartAgents(AgentRunner* agent_runner,
                                       std::vector<std::string> session_agents,
                                       std::vector<std::string> startup_agents) {
  FX_LOGS(INFO) << "Starting session_agents:";
  for (const auto& agent : session_agents) {
    FX_LOGS(INFO) << " " << agent;
    StartSessionAgent(agent_runner, agent);
  }

  FX_LOGS(INFO) << "Starting startup_agents:";
  for (const auto& agent : startup_agents) {
    FX_LOGS(INFO) << " " << agent;
    StartAgent(agent_runner, agent);
  }
}

fuchsia::sys::ServiceList StartupAgentLauncher::GetServicesForAgent(std::string agent_url) {
  fuchsia::sys::ServiceList service_list;
  agent_namespaces_.emplace_back(service_list.provider.NewRequest());
  service_list.names = AddAgentServices(agent_url, &agent_namespaces_.back());
  return service_list;
}

void StartupAgentLauncher::StartAgent(AgentRunner* agent_runner, const std::string& url) {
  fuchsia::sys::ServiceProviderPtr services;
  agent_runner->ConnectToAgent(kInternalAgentRunnerRequestorUrl, url, services.NewRequest(),
                               /*agent_controller=*/nullptr);
}

void StartupAgentLauncher::StartSessionAgent(AgentRunner* agent_runner, const std::string& url) {
  SessionAgentData* const agent_data = &session_agents_[url];

  agent_runner->ConnectToAgent(kInternalAgentRunnerRequestorUrl, url,
                               agent_data->services.NewRequest(),
                               agent_data->controller.NewRequest());

  // complete any pending connection requests
  for (auto& request : agent_data->pending_service_requests) {
    agent_data->services->ConnectToService(request.name, std::move(request.channel));
  }
  agent_data->pending_service_requests.clear();

  // fuchsia::modular::Agent runner closes the agent controller connection when
  // the agent terminates. We restart the agent (up to a limit) when we notice
  // this.
  //
  // NOTE(rosswang,mesch): Although the interface we're actually interested in
  // is |data[url].services|, we still need to put the restart handler on the
  // controller. When the agent crashes, |data[url].services| often gets closed
  // quite a bit earlier (~1 second) than the agent runner notices via the
  // application controller (which it must use as opposed to any interface on
  // the agent itself since the agent is not required to implement any
  // interfaces itself, even though it is recommended that it does). If we try
  // to restart the agent at that time, the agent runner would attempt to simply
  // send the connection request to the crashed agent instance and not relaunch
  // the agent.
  //
  // It is also because of this delay that we must queue any pending service
  // connection requests until we can restart.
  agent_data->controller.set_error_handler([this, agent_runner, url](zx_status_t status) {
    auto it = session_agents_.find(url);
    FX_DCHECK(it != session_agents_.end()) << "Controller and services not registered for " << url;
    if (is_terminating_cb_ != nullptr && is_terminating_cb_()) {
      FX_LOGS(INFO) << "Session agent " << url << " has terminated, as expected, during shutdown.";
      return;
    }

    FX_LOGS(INFO) << "Session agent " << url << " has terminated unexpectedly.";
    auto& agent_data = it->second;
    agent_data.services.Unbind();
    agent_data.controller.Unbind();
    if (agent_data.restart.ShouldRetry()) {
      FX_LOGS(INFO) << "Restarting " << url << "...";
      StartSessionAgent(agent_runner, url);
    } else {
      FX_LOGS(WARNING) << url << " failed to restart more than " << kSessionAgentRetryLimit.count
                       << " times in " << kSessionAgentRetryLimit.period.to_secs() << " seconds.";
      // Erase so that incoming connection requests fail fast rather than
      // enqueue forever.
      session_agents_.erase(it);
    }
  });
}

std::vector<std::string> StartupAgentLauncher::AddAgentServices(
    const std::string& url, component::ServiceNamespace* service_namespace) {
  std::vector<std::string> service_names;

  if (session_agents_.find(url) != session_agents_.end()) {
    // All services added below should be exclusive to session agents.
    service_names.push_back(fuchsia::modular::PuppetMaster::Name_);
    service_namespace->AddService<fuchsia::modular::PuppetMaster>(
        [this](auto request) { puppet_master_connector_(std::move(request)); });

    service_names.push_back(fuchsia::modular::SessionRestartController::Name_);
    service_namespace->AddService<fuchsia::modular::SessionRestartController>(
        [this, url](auto request) { session_restart_controller_connector_(std::move(request)); });

    service_names.push_back(fuchsia::intl::PropertyProvider::Name_);
    service_namespace->AddService<fuchsia::intl::PropertyProvider>(
        [this, url](auto request) { intl_property_provider_connector_(std::move(request)); });
  }

  for (const auto& name : additional_services_for_agents_.names) {
    service_names.push_back(name);
    service_namespace->AddServiceForName(
        [this, name](auto request) {
          auto status = additional_services_for_agents_directory_.Connect(name, std::move(request));
          if (status != ZX_OK) {
            FX_PLOGS(WARNING, status) << "Could not connect to service " << name
                                      << " provided by the session launcher component.";
          }
        },
        name);
  }

  return service_names;
}

}  // namespace modular
