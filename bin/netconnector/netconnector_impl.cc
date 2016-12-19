// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/src/netconnector_impl.h"

#include "apps/netconnector/src/netconnector_params.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace netconnector {

NetConnectorImpl::NetConnectorImpl(NetConnectorParams* params)
    : params_(params),
      application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()) {
  if (!params->listen()) {
    // Start the listener.
    FTL_DCHECK(!params->listen());

    netconnector::NetConnectorPtr connector =
        application_context_
            ->ConnectToEnvironmentService<netconnector::NetConnector>();

    if (!params->host_name().empty()) {
      connector->SetHostName(params->host_name());
    }

    for (auto& pair : params_->responders()) {
      connector->RegisterResponder(pair.first, pair.second);
    }

    for (auto& pair : params_->devices()) {
      connector->RegisterDevice(pair.first, pair.second);
    }

    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    return;
  }

  // Running as the listener.

  if (!params->host_name().empty()) {
    SetHostName(params->host_name());
  }

  listener_.Start(kPort, [this](ftl::UniqueFD fd) {
    AddResponderAgent(ResponderAgent::Create(std::move(fd), this));
  });

  application_context_->outgoing_services()->AddService<NetConnector>(
      [this](fidl::InterfaceRequest<NetConnector> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

NetConnectorImpl::~NetConnectorImpl() {}

ResponderPtr NetConnectorImpl::GetResponder(const std::string responder_name) {
  auto iter = params_->responders().find(responder_name);
  if (iter == params_->responders().end()) {
    FTL_LOG(WARNING) << "Requested responder name '" << responder_name
                     << "' not found";
    return ResponderPtr();
  }

  return application_context_->ConnectToEnvironmentService<Responder>(
      iter->second);
}

void NetConnectorImpl::ReleaseRequestorAgent(RequestorAgent* requestor_agent) {
  size_t removed = requestor_agents_.erase(requestor_agent);
  FTL_DCHECK(removed == 1);
}

void NetConnectorImpl::ReleaseResponderAgent(ResponderAgent* responder_agent) {
  size_t removed = responder_agents_.erase(responder_agent);
  FTL_DCHECK(removed == 1);
}

void NetConnectorImpl::SetHostName(const fidl::String& host_name) {
  FTL_LOG(INFO) << "Host name set to '" << host_name << "'";
  // We have no use for the host name until mDNS is implemented.
  // TODO(dalesat): Implement mDNS.
}

void NetConnectorImpl::RegisterResponder(const fidl::String& name,
                                         const fidl::String& service_name) {
  FTL_LOG(INFO) << "Responder '" << name << "' registered with service name "
                << service_name;
  params_->RegisterResponder(name, service_name);
}

void NetConnectorImpl::RegisterDevice(const fidl::String& name,
                                      const fidl::String& address) {
  FTL_LOG(INFO) << "Device '" << name << "' registered at address " << address;
  params_->RegisterDevice(name, address);
}

void NetConnectorImpl::RequestConnection(const fidl::String& device_name,
                                         const fidl::String& responder_name,
                                         mx::channel channel) {
  auto iter = params_->devices().find(device_name);
  if (iter == params_->devices().end()) {
    FTL_LOG(ERROR) << "Unrecognized device name " << device_name;
    return;
  }

  std::unique_ptr<RequestorAgent> requestor_agent = RequestorAgent::Create(
      iter->second, kPort, responder_name, std::move(channel), this);

  if (!requestor_agent) {
    FTL_LOG(ERROR) << "Connection failed";
    return;
  }

  AddRequestorAgent(std::move(requestor_agent));
}

void NetConnectorImpl::AddRequestorAgent(
    std::unique_ptr<RequestorAgent> requestor_agent) {
  RequestorAgent* raw_ptr = requestor_agent.get();
  requestor_agents_.emplace(raw_ptr, std::move(requestor_agent));
}

void NetConnectorImpl::AddResponderAgent(
    std::unique_ptr<ResponderAgent> responder_agent) {
  ResponderAgent* raw_ptr = responder_agent.get();
  responder_agents_.emplace(raw_ptr, std::move(responder_agent));
}

}  // namespace netconnector
