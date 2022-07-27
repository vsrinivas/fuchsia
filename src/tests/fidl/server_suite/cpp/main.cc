// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.serversuite/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

class TargetServer : public fidl::Server<fidl_serversuite::Target> {
 public:
  explicit TargetServer(fidl::ClientEnd<fidl_serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void OneWayNoPayload(OneWayNoPayloadRequest& request,
                       OneWayNoPayloadCompleter::Sync& completer) override {
    std::cout << "Target.OneWayNoPayload()" << std::endl;
    auto result = reporter_->ReceivedOneWayNoPayload();
    ZX_ASSERT(result.is_ok());
  }

  void TwoWayNoPayload(TwoWayNoPayloadRequest& request,
                       TwoWayNoPayloadCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayNoPayload()" << std::endl;
    completer.Reply();
  }

  void TwoWayStructPayload(TwoWayStructPayloadRequest& request,
                           TwoWayStructPayloadCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayStructPayload()" << std::endl;
    completer.Reply(request.v());
  }

  void TwoWayTablePayload(TwoWayTablePayloadRequest& request,
                          TwoWayTablePayloadCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayTablePayload()" << std::endl;
    fidl_serversuite::TargetTwoWayTablePayloadResponse response({.v = request.v()});
    completer.Reply(response);
  }

  void TwoWayUnionPayload(TwoWayUnionPayloadRequest& request,
                          TwoWayUnionPayloadCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayUnionPayload()" << std::endl;
    ZX_ASSERT(request.v().has_value());
    completer.Reply(fidl_serversuite::TargetTwoWayUnionPayloadResponse::WithV(request.v().value()));
  }

  void TwoWayResult(TwoWayResultRequest& request, TwoWayResultCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayResult()" << std::endl;
    switch (request.Which()) {
      case TwoWayResultRequest::Tag::kPayload:
        completer.Reply(fitx::ok(request.payload().value()));
        return;
      case TwoWayResultRequest::Tag::kError:
        completer.Reply(fitx::error(request.error().value()));
        return;
    }
    ZX_PANIC("unhandled case");
  }

  void GetHandleRights(GetHandleRightsRequest& request,
                       GetHandleRightsCompleter::Sync& completer) override {
    zx_info_handle_basic_t info;
    ZX_ASSERT(ZX_OK == request.handle().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                                                 nullptr));
    completer.Reply(info.rights);
  }

  void GetSignalableEventRights(GetSignalableEventRightsRequest& request,
                                GetSignalableEventRightsCompleter::Sync& completer) override {
    zx_info_handle_basic_t info;
    ZX_ASSERT(ZX_OK == request.handle().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                                                 nullptr));
    completer.Reply(info.rights);
  }

  void EchoAsTransferableSignalableEvent(
      EchoAsTransferableSignalableEventRequest& request,
      EchoAsTransferableSignalableEventCompleter::Sync& completer) override {
    completer.Reply(zx::event(request.handle().release()));
  }

  void CloseWithEpitaph(CloseWithEpitaphRequest& request,
                        CloseWithEpitaphCompleter::Sync& completer) override {
    completer.Close(request.epitaph_status());
  }

 private:
  fidl::SyncClient<fidl_serversuite::Reporter> reporter_;
};

class RunnerServer : public fidl::Server<fidl_serversuite::Runner> {
 public:
  explicit RunnerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void IsTestEnabled(IsTestEnabledRequest& request,
                     IsTestEnabledCompleter::Sync& completer) override {
    bool is_enabled = [request]() {
      switch (request.test()) {
        case fidl_serversuite::Test::kOneWayWithNonZeroTxid:
        case fidl_serversuite::Test::kTwoWayNoPayloadWithZeroTxid:
        case fidl_serversuite::Test::kBadAtRestFlagsCausesClose:
        case fidl_serversuite::Test::kBadDynamicFlagsCausesClose:
          return false;
        default:
          return true;
      }
    }();
    completer.Reply(is_enabled);
  }

  void Start(StartRequest& request, StartCompleter::Sync& completer) override {
    std::cout << "Runner.Start()" << std::endl;

    auto target_server = std::make_unique<TargetServer>(std::move(request.reporter()));
    auto endpoints = fidl::CreateEndpoints<fidl_serversuite::Target>();
    fidl::BindServer(dispatcher_, std::move(endpoints->server), std::move(target_server),
                     [](auto*, fidl::UnbindInfo info, auto) {
                       if (!info.is_dispatcher_shutdown() && !info.is_user_initiated() &&
                           !info.is_peer_closed()) {
                         std::cout << "Target unbound with error: " << info.FormatDescription()
                                   << std::endl;
                       }
                     });

    completer.Reply(std::move(endpoints->client));
  }

  void CheckAlive(CheckAliveRequest& request, CheckAliveCompleter::Sync& completer) override {
    completer.Reply();
  }

 private:
  async_dispatcher_t* dispatcher_;
};

int main(int argc, const char** argv) {
  std::cout << "CPP server: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  RunnerServer runner_server(loop.dispatcher());
  auto result = outgoing.AddProtocol<fidl_serversuite::Runner>(&runner_server);
  ZX_ASSERT(result.is_ok());

  std::cout << "CPP server: ready!" << std::endl;
  return loop.Run();
}
