// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/49875): rewrite this test in Rust.

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netemul/sync/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>

#include <iostream>
#include <string>
#include <unordered_map>

#include "lib/fidl/cpp/interface_handle.h"
#include "src/connectivity/network/mdns/service/encoding/dns_message.h"
#include "src/connectivity/network/mdns/service/transport/mdns_transceiver.h"
#include "src/lib/inet/ip_address.h"

const std::string kLocalArgument = "--local";
const std::string kRemoteArgument = "--remote";
const std::string kInstanceName = "mdns_test_instance_name";
const std::string kHostName = "mdns_test_host_name";

const size_t kIterations = 100;
const zx_duration_t kTimeout = ZX_SEC(120);

const std::string kBusName = "netemul-test-bus";
const std::string kLocalClientName = "local";
const std::string kRemoteClientName = "remote";

const auto kLocalV4Addr = inet::IpAddress(192, 168, 0, 1);
const auto kRemoteV4Addr = inet::IpAddress(192, 168, 0, 2);

const auto kLocalV6LinkLocalAddr = inet::IpAddress(0xfe80, 0, 0, 0, 0, 0, 0, 0x001);
const auto kRemoteV6LinkLocalAddr = inet::IpAddress(0xfe80, 0, 0, 0, 0, 0, 0, 0x002);

const auto kLocalV6Addr = inet::IpAddress(0x2001, 0, 0, 0, 0, 0, 0, 0x001);
const auto kRemoteV6Addr = inet::IpAddress(0x2001, 0, 0, 0, 0, 0, 0, 0x002);

const auto kRemoteTransceiverAddrs =
    std::vector<inet::IpAddress>{kRemoteV4Addr, kRemoteV6LinkLocalAddr};
const auto kLocalTransceiverAddrs =
    std::vector<inet::IpAddress>{kLocalV4Addr, kLocalV6LinkLocalAddr};

// All possible connections keyed on sender's address.
std::unordered_map<inet::IpAddress, inet::IpAddress> connections(
    {{kLocalV4Addr, kRemoteV4Addr},
     {kRemoteV4Addr, kLocalV4Addr},

     {kLocalV6LinkLocalAddr, kRemoteV6LinkLocalAddr},
     {kRemoteV6LinkLocalAddr, kLocalV6LinkLocalAddr}});

namespace mdns::test {

using QuitCallback = fit::function<void(int)>;

////////////////////////////////////////////////////////////////////////////////
// TestAgent
//
// This test verifies that MDNS transceivers don't receive messages leaked from
// other connections.
class TestAgent {
 public:
  static std::unique_ptr<TestAgent> Create(sys::ComponentContext* component_context,
                                           std::string client_name, QuitCallback quit_callback) {
    return std::make_unique<TestAgent>(component_context, client_name, std::move(quit_callback));
  }

  TestAgent(sys::ComponentContext* component_context, std::string client_name,
            QuitCallback quit_callback)
      : client_name_(client_name),
        component_context_(component_context),
        quit_callback_(std::move(quit_callback)) {
    auto net_interfaces_state =
        component_context_->svc()->Connect<fuchsia::net::interfaces::State>();
    fuchsia::net::interfaces::WatcherPtr watcher;
    net_interfaces_state->GetWatcher(fuchsia::net::interfaces::WatcherOptions(),
                                     watcher.NewRequest());
    watcher.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: interface watcher channel disconnected unexpectedly, status="
                << zx_status_get_string(status) << std::endl;
      Quit(1);
    });

    transceiver_.Start(
        std::move(watcher),
        // This lambda is called when interface changes are detected.
        [this]() {
          if (sending_) {
            return;
          }
          // Wait for transceivers to start on all interfaces.
          for (const auto& addr : TransceiverAddrs()) {
            if (transceiver_.GetInterfaceTransceiver(addr) == nullptr) {
              return;
            }
          }
          std::cerr << "All interfaces are ready, waiting for other test agents.\n";
          sending_ = true;

          // Subscribe to bus after interfaces are ready. This signals other
          // test agents that are waiting on the bus.
          OpenBus();
          bus_proxy_->WaitForClients(
              {kLocalClientName, kRemoteClientName}, kTimeout,
              [this](bool result, fidl::VectorPtr<std::string> absent) {
                if (!result) {
                  std::cerr
                      << "FAILED: timed out waiting for clients to start, missing clients: \n";
                  for (auto name : *absent) {
                    std::cerr << name << "\n";
                  }
                  Quit(1);
                }
                std::cerr << "All test agents are ready, sending requests.\n";
                // Send requests for multiple iterations so leaked messages
                // from previous iterations can be discovered.
                for (size_t i = 0; i < kIterations; i++) {
                  SendRequest();
                }
              });
        },
        // This lambda is called when inbound MDNS messages are received.
        [this](std::unique_ptr<DnsMessage> message, const ReplyAddress& reply_address) {
          auto interface_addr = reply_address.interface_address();
          auto from = reply_address.socket_address().address();

          for (const auto& resource : message->authorities_) {
            if ((resource->type_ == DnsType::kA) && (resource->a_.address_.address_ != from)) {
              std::cerr << "FAILED: unexpected address " << resource->a_.address_.address_
                        << " received in A resource from " << from << ".\n";
              Quit(1);
            }
            if ((resource->type_ == DnsType::kAaaa) &&
                (resource->aaaa_.address_.address_ != from)) {
              std::cerr << "FAILED: unexpected address " << resource->aaaa_.address_.address_
                        << " received in AAAA resource from " << from << ".\n";
              Quit(1);
            }
          }

          auto connection = connections.find(from);
          if (connection == connections.end()) {
            std::cerr << "FAILED: received message from unexpected address " << from << ".\n";
            Quit(1);
          }

          if (connection->second != interface_addr) {
            std::cerr << "FAILED: got unexpected message on interface " << interface_addr
                      << ", from " << from << ".\n";
            Quit(1);
          }

          response_count_++;
          // Transceiver sends out requests on all interfaces. Each interface
          // has exactly one IP address, so one response should be received
          // for each address.
          if (response_count_ >= TransceiverAddrs().size() * kIterations) {
            Quit(0);
          }
        },
        MdnsInterfaceTransceiver::Create);
  }

  const std::vector<inet::IpAddress>& TransceiverAddrs() {
    return client_name_ == kLocalClientName ? kLocalTransceiverAddrs : kRemoteTransceiverAddrs;
  }

  void OpenBus() {
    auto syncm = component_context_->svc()->Connect<fuchsia::netemul::sync::SyncManager>();
    syncm.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: SyncManager channel disconnected unexpectedly, status " << status
                << ".\n";
      Quit(1);
    });

    fidl::InterfaceHandle<fuchsia::netemul::sync::Bus> bus_handle;
    auto request = bus_handle.NewRequest();
    syncm->BusSubscribe(kBusName, client_name_, std::move(request));

    bus_proxy_ = bus_handle.Bind();
    bus_proxy_.set_error_handler([this](zx_status_t status) {
      std::cerr << "FAILED: Bus channel disconnected unexpectedly, status " << status << ".\n";
      Quit(1);
    });
  }

  void SendRequest() {
    DnsMessage message;
    message.questions_.push_back(std::make_shared<DnsQuestion>(kInstanceName, DnsType::kAaaa));
    // Add address placeholder resource.
    message.authorities_.push_back(std::make_shared<DnsResource>(kHostName, DnsType::kA));
    message.UpdateCounts();
    transceiver_.SendMessage(std::move(message),
                             ReplyAddress::Multicast(Media::kBoth, IpVersions::kBoth));
  }

  void Quit(int exit_code) {
    FX_DCHECK(quit_callback_);

    bus_proxy_.set_error_handler(nullptr);
    bus_proxy_.Unbind();
    transceiver_.Stop();
    quit_callback_(exit_code);
  }

  size_t response_count_ = 0;
  bool sending_ = false;
  // client_name_ is used to subscribe to and wait on the Bus provided by
  // netemul, so multiple test agents can wait for others to come online before
  // sending out requests.
  std::string client_name_;
  fuchsia::netemul::sync::BusPtr bus_proxy_;
  MdnsTransceiver transceiver_;
  sys::ComponentContext* component_context_;
  QuitCallback quit_callback_;
};

}  // namespace mdns::test

////////////////////////////////////////////////////////////////////////////////
// main
//
int main(int argc, const char** argv) {
  bool local = false;
  bool remote = false;

  for (int arg_index = 1; arg_index < argc; ++arg_index) {
    auto arg = argv[arg_index];
    if (arg == kLocalArgument) {
      local = true;
    } else if (arg == kRemoteArgument) {
      remote = true;
    } else {
      std::cerr << "Unknown flag: " << arg << ".\n";
      return 1;
    }
  }

  if (local == remote) {
    std::cerr << "One and only one of these flags should be specified: " << kLocalArgument << " | "
              << kRemoteArgument << ".\n";
    return 1;
  }

  std::string client_name;
  if (local) {
    client_name = kLocalClientName;
  } else {
    client_name = kRemoteClientName;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();
  int result = 0;
  // The test setup is symmetric. Test agents running the same code are started
  // in both environments.
  auto test_agent = mdns::test::TestAgent::Create(
      component_context.get(), client_name, [&loop, &result](int exit_code) {
        result = exit_code;
        async::PostTask(loop.dispatcher(), [&loop]() { loop.Quit(); });
      });
  loop.Run();
  return result;
}
