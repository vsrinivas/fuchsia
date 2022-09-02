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

class ClosedTargetServer : public fidl::serversuite::ClosedTarget {
 public:
  explicit ClosedTargetServer(fidl::InterfacePtr<fidl::serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void OneWayNoPayload() override {
    std::cout << "ClosedTarget.OneWayNoPayload()" << std::endl;
    reporter_->ReceivedOneWayNoPayload();
  }

  void TwoWayNoPayload(TwoWayNoPayloadCallback callback) override {
    std::cout << "ClosedTarget.TwoWayNoPayload()" << std::endl;
    callback();
  }

  void TwoWayStructPayload(int8_t v, TwoWayStructPayloadCallback callback) override {
    std::cout << "ClosedTarget.TwoWayStructPayload()" << std::endl;
    callback(v);
  }

  void TwoWayTablePayload(::fidl::serversuite::ClosedTargetTwoWayTablePayloadRequest request,
                          TwoWayTablePayloadCallback callback) override {
    std::cout << "ClosedTarget.TwoWayTablePayload()" << std::endl;
    fidl::serversuite::ClosedTargetTwoWayTablePayloadResponse response;
    response.set_v(request.v());
    callback(std::move(response));
  }

  void TwoWayUnionPayload(::fidl::serversuite::ClosedTargetTwoWayUnionPayloadRequest request,
                          TwoWayUnionPayloadCallback callback) override {
    std::cout << "ClosedTarget.TwoWayUnionPayload()" << std::endl;
    fidl::serversuite::ClosedTargetTwoWayUnionPayloadResponse response;
    response.set_v(request.v());
    callback(std::move(response));
  }

  void TwoWayResult(::fidl::serversuite::ClosedTargetTwoWayResultRequest request,
                    TwoWayResultCallback callback) override {
    std::cout << "ClosedTarget.TwoWayResult()" << std::endl;
    switch (request.Which()) {
      case fidl::serversuite::ClosedTargetTwoWayResultRequest::kPayload:
        callback(fidl::serversuite::ClosedTarget_TwoWayResult_Result::WithResponse(
            fidl::serversuite::ClosedTarget_TwoWayResult_Response(request.payload())));
        break;
      case fidl::serversuite::ClosedTargetTwoWayResultRequest::kError:
        callback(fidl::serversuite::ClosedTarget_TwoWayResult_Result::WithErr(
            std::move(request.error())));
        break;
      case fidl::serversuite::ClosedTargetTwoWayResultRequest::Invalid:
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

  void CloseWithEpitaph(int32_t epitaph_status) override {
    ZX_ASSERT(ZX_OK == binding_->Close(epitaph_status));
  }

  void ByteVectorSize(std::vector<uint8_t> vec, ByteVectorSizeCallback callback) override {
    callback(static_cast<uint32_t>(vec.size()));
  }

  void HandleVectorSize(std::vector<zx::event> vec, HandleVectorSizeCallback callback) override {
    callback(static_cast<uint32_t>(vec.size()));
  }

  void CreateNByteVector(uint32_t n, CreateNByteVectorCallback callback) override {
    std::vector<uint8_t> bytes(n);
    callback(std::move(bytes));
  }

  void CreateNHandleVector(uint32_t n, CreateNHandleVectorCallback callback) override {
    std::vector<zx::event> handles(n);
    for (auto& handle : handles) {
      ZX_ASSERT(ZX_OK == zx::event::create(0, &handle));
    }
    callback(std::move(handles));
  }

  void set_binding(fidl::Binding<fidl::serversuite::ClosedTarget>* binding) { binding_ = binding; }

 private:
  fidl::InterfacePtr<fidl::serversuite::Reporter> reporter_;
  fidl::Binding<fidl::serversuite::ClosedTarget>* binding_ = nullptr;
};

class RunnerServer : public fidl::serversuite::Runner {
 public:
  explicit RunnerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void IsTestEnabled(fidl::serversuite::Test test, IsTestEnabledCallback callback) override {
    switch (test) {
      case fidl::serversuite::Test::BAD_AT_REST_FLAGS_CAUSES_CLOSE:
      case fidl::serversuite::Test::BAD_DYNAMIC_FLAGS_CAUSES_CLOSE:
      case fidl::serversuite::Test::SERVER_SENDS_TOO_FEW_RIGHTS:
      case fidl::serversuite::Test::RESPONSE_EXCEEDS_BYTE_LIMIT:
      case fidl::serversuite::Test::RESPONSE_EXCEEDS_HANDLE_LIMIT:
        callback(false);
        return;
      default:
        if ((uint32_t)test / 1000 == 7) {
          // Disable all unknown interactions tests.
          callback(false);
        } else {
          callback(true);
        }
        return;
    }
  }

  void Start(fidl::InterfaceHandle<fidl::serversuite::Reporter> reporter,
             fidl::serversuite::AnyTarget target, StartCallback callback) override {
    if (target.is_closed_target()) {
      target_server_ = std::make_unique<ClosedTargetServer>(reporter.Bind());
      target_binding_ =
          std::make_unique<fidl::Binding<fidl::serversuite::ClosedTarget>>(target_server_.get());
      target_server_->set_binding(target_binding_.get());

      target_binding_->Bind(std::move(target.closed_target()), dispatcher_);
      callback();
    } else {
      // TODO(fxbug.dev/88366): cover the other target types.
      ZX_PANIC("HLCPP does not support open or ajar protocols yet.");
    }
  }

  void CheckAlive(CheckAliveCallback callback) override { return callback(); }

 private:
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<ClosedTargetServer> target_server_;
  std::unique_ptr<fidl::Binding<fidl::serversuite::ClosedTarget>> target_binding_;
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
