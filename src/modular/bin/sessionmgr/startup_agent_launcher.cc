// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/startup_agent_launcher.h"

#include <fuchsia/bluetooth/le/cpp/fidl.h>
#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/svc/cpp/service_namespace.h>
#include <zircon/status.h>

#include "src/lib/files/file.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/lib/connect/connect.h"

namespace modular {

using cobalt_registry::SessionAgentEventsMetricDimensionEventType;

namespace {

static constexpr modular::RateLimitedRetry::Threshold kSessionAgentRetryLimit = {3, zx::sec(45)};

static constexpr char kInternalAgentRunnerRequestorUrl[] = "builtin://modular";

}  // namespace

template <class Interface>
StartupAgentLauncher::SessionAgentData::DeferredInterfaceRequest::DeferredInterfaceRequest(
    fidl::InterfaceRequest<Interface> request)
    : name(Interface::Name_), channel(request.TakeChannel()) {}

StartupAgentLauncher::SessionAgentData::SessionAgentData()
    : restart(kSessionAgentRetryLimit) {}

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
    fidl::InterfaceRequestHandler<fuchsia::modular::FocusProvider> focus_provider_connector,
    fidl::InterfaceRequestHandler<fuchsia::modular::PuppetMaster> puppet_master_connector,
    fidl::InterfaceRequestHandler<fuchsia::intl::PropertyProvider> intl_property_provider_connector,
    fit::function<bool()> is_terminating_cb)
    : focus_provider_connector_(std::move(focus_provider_connector)),
      puppet_master_connector_(std::move(puppet_master_connector)),
      intl_property_provider_connector_(std::move(intl_property_provider_connector)),
      is_terminating_cb_(std::move(is_terminating_cb)){};

void StartupAgentLauncher::StartAgents(AgentRunner* agent_runner,
                                               std::vector<std::string> session_agents,
                                               std::vector<std::string> startup_agents) {
  FXL_LOG(INFO) << "Starting session_agents:";
  for (const auto& agent : session_agents) {
    FXL_LOG(INFO) << " " << agent;
    StartSessionAgent(agent_runner, agent);
  }

  FXL_LOG(INFO) << "Starting startup_agents:";
  for (const auto& agent : startup_agents) {
    FXL_LOG(INFO) << " " << agent;
    StartAgent(agent_runner, agent);
  }
}

fuchsia::sys::ServiceList StartupAgentLauncher::GetServicesForAgent(std::string agent_url) {
  fuchsia::sys::ServiceList service_list;
  agent_namespaces_.emplace_back(service_list.provider.NewRequest());
  auto* agent_host = &agent_namespaces_.back();
  service_list.names = AddAgentServices(agent_url, agent_host);
  return service_list;
}

void StartupAgentLauncher::StartAgent(AgentRunner* agent_runner, const std::string& url) {
  fuchsia::modular::AgentControllerPtr controller;
  fuchsia::sys::ServiceProviderPtr services;
  agent_runner->ConnectToAgent(kInternalAgentRunnerRequestorUrl, url, services.NewRequest(),
                               controller.NewRequest());
  agent_controllers_.push_back(std::move(controller));
}

void StartupAgentLauncher::StartSessionAgent(AgentRunner* agent_runner,
                                                     const std::string& url) {
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
    FXL_DCHECK(it != session_agents_.end()) << "Controller and services not registered for " << url;
    FXL_LOG(INFO) << url << " session agent appears to have crashed, with status: "
                  << zx_status_get_string(status);
    auto& agent_data = it->second;
    agent_data.services.Unbind();
    agent_data.controller.Unbind();
    ReportSessionAgentEvent(url, SessionAgentEventsMetricDimensionEventType::Crash);

    if (is_terminating_cb_ != nullptr && is_terminating_cb_()) {
      FXL_LOG(INFO) << "Not restarting " << url
                    << " because StartupAgentLauncher is terminating.";
    } else {
      if (agent_data.restart.ShouldRetry()) {
        FXL_LOG(INFO) << "Restarting " << url << "...";
        StartSessionAgent(agent_runner, url);
      } else {
        FXL_LOG(WARNING) << url << " failed to restart more than " << kSessionAgentRetryLimit.count
                         << " times in " << kSessionAgentRetryLimit.period.to_secs() << " seconds.";
        ReportSessionAgentEvent(url,
                                SessionAgentEventsMetricDimensionEventType::CrashLimitExceeded);
        // Erase so that incoming connection requests fail fast rather than
        // enqueue forever.
        session_agents_.erase(it);
      }
    }
  });
}

std::vector<std::string> StartupAgentLauncher::AddAgentServices(
    const std::string& url, component::ServiceNamespace* agent_host) {
  std::vector<std::string> service_names;

  if (session_agents_.find(url) != session_agents_.end()) {
    // All services added below should be exclusive to session agents.
    service_names.push_back(fuchsia::modular::PuppetMaster::Name_);
    agent_host->AddService<fuchsia::modular::PuppetMaster>(
        [this](fidl::InterfaceRequest<fuchsia::modular::PuppetMaster> request) {
          puppet_master_connector_(std::move(request));
        });

    service_names.push_back(fuchsia::modular::FocusProvider::Name_);
    agent_host->AddService<fuchsia::modular::FocusProvider>(
        [this, url](fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) {
          focus_provider_connector_(std::move(request));
        });

    service_names.push_back(fuchsia::intl::PropertyProvider::Name_);
    agent_host->AddService<fuchsia::intl::PropertyProvider>(
        [this, url](fidl::InterfaceRequest<fuchsia::intl::PropertyProvider> request) {
          intl_property_provider_connector_(std::move(request));
        });
  }

  return service_names;
}

}  // namespace modular
