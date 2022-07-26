// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/serversuite/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

#include <iostream>

class TargetServer : public fidl::serversuite::Target {
 public:
  explicit TargetServer(fidl::InterfacePtr<fidl::serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void OneWayNoPayload() override {
    std::cout << "Target.OneWayNoPayload()" << std::endl;
    reporter_->ReceivedOneWayNoPayload();
  }

  void TwoWayNoPayload(TwoWayNoPayloadCallback callback) override {
    std::cout << "Target.TwoWayNoPayload()" << std::endl;
    callback();
  }

  void TwoWayStructPayload(int8_t v, TwoWayStructPayloadCallback callback) override {
    std::cout << "Target.TwoWayStructPayload()" << std::endl;
    callback(v);
  }

  void TwoWayTablePayload(::fidl::serversuite::TargetTwoWayTablePayloadRequest request,
                          TwoWayTablePayloadCallback callback) override {
    std::cout << "Target.TwoWayTablePayload()" << std::endl;
    fidl::serversuite::TargetTwoWayTablePayloadResponse response;
    response.set_v(request.v());
    callback(std::move(response));
  }

  void TwoWayUnionPayload(::fidl::serversuite::TargetTwoWayUnionPayloadRequest request,
                          TwoWayUnionPayloadCallback callback) override {
    std::cout << "Target.TwoWayUnionPayload()" << std::endl;
    fidl::serversuite::TargetTwoWayUnionPayloadResponse response;
    response.set_v(request.v());
    callback(std::move(response));
  }

  void TwoWayResult(::fidl::serversuite::TargetTwoWayResultRequest request,
                    TwoWayResultCallback callback) override {
    std::cout << "Target.TwoWayResult()" << std::endl;
    switch (request.Which()) {
      case fidl::serversuite::TargetTwoWayResultRequest::kPayload:
        callback(fidl::serversuite::Target_TwoWayResult_Result::WithResponse(
            fidl::serversuite::Target_TwoWayResult_Response(request.payload())));
        break;
      case fidl::serversuite::TargetTwoWayResultRequest::kError:
        callback(
            fidl::serversuite::Target_TwoWayResult_Result::WithErr(std::move(request.error())));
        break;
      case fidl::serversuite::TargetTwoWayResultRequest::Invalid:
        ZX_PANIC("unexpected invalid case");
        break;
    }
  }

  void GetHandleRights(zx::handle handle, GetHandleRightsCallback callback) override {
    zx_info_handle_basic_t info;
    ZX_ASSERT(ZX_OK ==
              handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    callback(info.rights);
  }

  void GetSignalableEventRights(zx::event event,
                                GetSignalableEventRightsCallback callback) override {
    zx_info_handle_basic_t info;
    ZX_ASSERT(ZX_OK == event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    callback(info.rights);
  }

  void EchoAsTransferableSignalableEvent(
      zx::handle handle, EchoAsTransferableSignalableEventCallback callback) override {
    callback(zx::event(handle.release()));
  }

 private:
  fidl::InterfacePtr<fidl::serversuite::Reporter> reporter_;
};

class RunnerServer : public fidl::serversuite::Runner {
 public:
  explicit RunnerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void IsTestEnabled(fidl::serversuite::Test test, IsTestEnabledCallback callback) override {
    switch (test) {
      case fidl::serversuite::Test::BAD_AT_REST_FLAGS_CAUSES_CLOSE:
      case fidl::serversuite::Test::BAD_DYNAMIC_FLAGS_CAUSES_CLOSE:
      case fidl::serversuite::Test::SERVER_SENDS_TOO_FEW_RIGHTS:
        callback(false);
        return;
      default:
        callback(true);
        return;
    }
  }

  void Start(fidl::InterfaceHandle<fidl::serversuite::Reporter> reporter,
             StartCallback callback) override {
    target_server_ = std::make_unique<TargetServer>(reporter.Bind());
    target_binding_ =
        std::make_unique<fidl::Binding<fidl::serversuite::Target>>(target_server_.get());

    zx::channel client_end, server_end;
    ZX_ASSERT(ZX_OK == zx::channel::create(0, &client_end, &server_end));
    target_binding_->Bind(fidl::InterfaceRequest<fidl::serversuite::Target>(std::move(server_end)),
                          dispatcher_);
    callback(fidl::InterfaceHandle<fidl::serversuite::Target>(std::move(client_end)));
  }

  void CheckAlive(CheckAliveCallback callback) override { return callback(); }

 private:
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<TargetServer> target_server_;
  std::unique_ptr<fidl::Binding<fidl::serversuite::Target>> target_binding_;
};

int main(int argc, const char** argv) {
  std::cout << "HLCPP server: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  RunnerServer server(loop.dispatcher());
  fidl::Binding<fidl::serversuite::Runner> binding(&server);
  fidl::InterfaceRequestHandler<fidl::serversuite::Runner> handler =
      [&](fidl::InterfaceRequest<fidl::serversuite::Runner> server_end) {
        binding.Bind(std::move(server_end));
      };
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(std::move(handler));

  std::cout << "HLCPP server: ready!" << std::endl;
  return loop.Run();
}
