// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.serversuite/cpp/wire_messaging.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

class ClosedTargetServer : public fidl::WireServer<fidl_serversuite::ClosedTarget> {
 public:
  explicit ClosedTargetServer(fidl::ClientEnd<fidl_serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void OneWayNoPayload(OneWayNoPayloadCompleter::Sync& completer) override {
    std::cout << "Target.OneWayNoPayload()" << std::endl;
    auto result = reporter_->ReceivedOneWayNoPayload();
    ZX_ASSERT(result.ok());
  }

  void TwoWayNoPayload(TwoWayNoPayloadCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayNoPayload()" << std::endl;
    completer.Reply();
  }

  void TwoWayStructPayload(TwoWayStructPayloadRequestView request,
                           TwoWayStructPayloadCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayStructPayload()" << std::endl;
    completer.Reply(request->v);
  }

  void TwoWayTablePayload(TwoWayTablePayloadRequestView request,
                          TwoWayTablePayloadCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayTablePayload()" << std::endl;
    fidl::Arena arena;
    completer.Reply(fidl_serversuite::wire::ClosedTargetTwoWayTablePayloadResponse::Builder(arena)
                        .v(request->v())
                        .Build());
  }

  void TwoWayUnionPayload(TwoWayUnionPayloadRequestView request,
                          TwoWayUnionPayloadCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayUnionPayload()" << std::endl;
    completer.Reply(
        fidl_serversuite::wire::ClosedTargetTwoWayUnionPayloadResponse::WithV(request->v()));
  }

  void TwoWayResult(TwoWayResultRequestView request,
                    TwoWayResultCompleter::Sync& completer) override {
    std::cout << "Target.TwoWayResult()" << std::endl;
    switch (request->Which()) {
      case fidl_serversuite::wire::ClosedTargetTwoWayResultRequest::Tag::kPayload: {
        completer.ReplySuccess(request->payload());
        return;
      }
      case fidl_serversuite::wire::ClosedTargetTwoWayResultRequest::Tag::kError:
        completer.ReplyError(request->error());
        return;
    }
    ZX_PANIC("unhandled case");
  }

  void GetHandleRights(GetHandleRightsRequestView request,
                       GetHandleRightsCompleter::Sync& completer) override {
    zx_info_handle_basic_t info;
    ZX_ASSERT(ZX_OK == request->handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                                                nullptr));
    completer.Reply(info.rights);
  }

  void GetSignalableEventRights(GetSignalableEventRightsRequestView request,
                                GetSignalableEventRightsCompleter::Sync& completer) override {
    zx_info_handle_basic_t info;
    ZX_ASSERT(ZX_OK == request->handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr,
                                                nullptr));
    completer.Reply(info.rights);
  }

  void EchoAsTransferableSignalableEvent(
      EchoAsTransferableSignalableEventRequestView request,
      EchoAsTransferableSignalableEventCompleter::Sync& completer) override {
    completer.Reply(zx::event(request->handle.release()));
  }

  void CloseWithEpitaph(CloseWithEpitaphRequestView request,
                        CloseWithEpitaphCompleter::Sync& completer) override {
    completer.Close(request->epitaph_status);
  }

  void ByteVectorSize(ByteVectorSizeRequestView request,
                      ByteVectorSizeCompleter::Sync& completer) override {
    completer.Reply(static_cast<uint32_t>(request->vec.count()));
  }

  void HandleVectorSize(HandleVectorSizeRequestView request,
                        HandleVectorSizeCompleter::Sync& completer) override {
    completer.Reply(static_cast<uint32_t>(request->vec.count()));
  }

  void CreateNByteVector(CreateNByteVectorRequestView request,
                         CreateNByteVectorCompleter::Sync& completer) override {
    std::vector<uint8_t> bytes(request->n);
    completer.Reply(fidl::VectorView<uint8_t>::FromExternal(bytes));
  }

  void CreateNHandleVector(CreateNHandleVectorRequestView request,
                           CreateNHandleVectorCompleter::Sync& completer) override {
    std::vector<zx::event> handles(request->n);
    for (auto& handle : handles) {
      ZX_ASSERT(ZX_OK == zx::event::create(0, &handle));
    }
    completer.Reply(fidl::VectorView<zx::event>::FromExternal(handles));
  }

 private:
  fidl::WireSyncClient<fidl_serversuite::Reporter> reporter_;
};

class AjarTargetServer : public fidl::WireServer<fidl_serversuite::AjarTarget> {
 public:
  explicit AjarTargetServer(fidl::ClientEnd<fidl_serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void handle_unknown_method(fidl::UnknownMethodMetadata<fidl_serversuite::AjarTarget> metadata,
                             fidl::UnknownMethodCompleter::Sync& completer) override {
    auto result = reporter_->ReceivedUnknownMethod(
        metadata.method_ordinal, fidl_serversuite::wire::UnknownMethodType::kOneWay);
    ZX_ASSERT(result.ok());
  }

 private:
  fidl::WireSyncClient<fidl_serversuite::Reporter> reporter_;
};

class OpenTargetServer : public fidl::WireServer<fidl_serversuite::OpenTarget> {
 public:
  explicit OpenTargetServer(fidl::ClientEnd<fidl_serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void SendEvent(SendEventRequestView request, SendEventCompleter::Sync& completer) override {
    ZX_ASSERT_MSG(binding_ref_, "missing binding ref");
    switch (request->event_type) {
      case fidl_serversuite::wire::EventType::kStrict: {
        auto result = fidl::WireSendEvent(binding_ref_.value())->StrictEvent();
        ZX_ASSERT(result.ok());
        break;
      }
      case fidl_serversuite::wire::EventType::kFlexible: {
        auto result = fidl::WireSendEvent(binding_ref_.value())->FlexibleEvent();
        ZX_ASSERT(result.ok());
        break;
      }
    }
  }

  void StrictOneWay(StrictOneWayCompleter::Sync& completer) override {
    auto result = reporter_->ReceivedStrictOneWay();
    ZX_ASSERT(result.ok());
  }

  void FlexibleOneWay(FlexibleOneWayCompleter::Sync& completer) override {
    auto result = reporter_->ReceivedFlexibleOneWay();
    ZX_ASSERT(result.ok());
  }

  void StrictTwoWay(StrictTwoWayCompleter::Sync& completer) override { completer.Reply(); }

  void StrictTwoWayFields(StrictTwoWayFieldsRequestView request,
                          StrictTwoWayFieldsCompleter::Sync& completer) override {
    completer.Reply(request->reply_with);
  }

  void StrictTwoWayErr(StrictTwoWayErrRequestView request,
                       StrictTwoWayErrCompleter::Sync& completer) override {
    switch (request->Which()) {
      case fidl_serversuite::wire::OpenTargetStrictTwoWayErrRequest::Tag::kReplySuccess:
        completer.ReplySuccess();
        break;
      case fidl_serversuite::wire::OpenTargetStrictTwoWayErrRequest::Tag::kReplyError:
        completer.ReplyError(request->reply_error());
        break;
    }
  }

  void StrictTwoWayFieldsErr(StrictTwoWayFieldsErrRequestView request,
                             StrictTwoWayFieldsErrCompleter::Sync& completer) override {
    switch (request->Which()) {
      case fidl_serversuite::wire::OpenTargetStrictTwoWayFieldsErrRequest::Tag::kReplySuccess:
        completer.ReplySuccess(request->reply_success());
        break;
      case fidl_serversuite::wire::OpenTargetStrictTwoWayFieldsErrRequest::Tag::kReplyError:
        completer.ReplyError(request->reply_error());
        break;
    }
  }

  void FlexibleTwoWay(FlexibleTwoWayCompleter::Sync& completer) override { completer.Reply(); }

  void FlexibleTwoWayFields(FlexibleTwoWayFieldsRequestView request,
                            FlexibleTwoWayFieldsCompleter::Sync& completer) override {
    completer.Reply(request->reply_with);
  }

  void FlexibleTwoWayErr(FlexibleTwoWayErrRequestView request,
                         FlexibleTwoWayErrCompleter::Sync& completer) override {
    switch (request->Which()) {
      case fidl_serversuite::wire::OpenTargetFlexibleTwoWayErrRequest::Tag::kReplySuccess:
        completer.ReplySuccess();
        break;
      case fidl_serversuite::wire::OpenTargetFlexibleTwoWayErrRequest::Tag::kReplyError:
        completer.ReplyError(request->reply_error());
        break;
    }
  }

  void FlexibleTwoWayFieldsErr(FlexibleTwoWayFieldsErrRequestView request,
                               FlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
    switch (request->Which()) {
      case fidl_serversuite::wire::OpenTargetFlexibleTwoWayFieldsErrRequest::Tag::kReplySuccess:
        completer.ReplySuccess(request->reply_success());
        break;
      case fidl_serversuite::wire::OpenTargetFlexibleTwoWayFieldsErrRequest::Tag::kReplyError:
        completer.ReplyError(request->reply_error());
        break;
    }
  }

  void handle_unknown_method(fidl::UnknownMethodMetadata<fidl_serversuite::OpenTarget> metadata,
                             fidl::UnknownMethodCompleter::Sync& completer) override {
    fidl_serversuite::wire::UnknownMethodType method_type;
    switch (metadata.unknown_interaction_type) {
      case fidl::UnknownMethodType::kOneWay:
        method_type = fidl_serversuite::wire::UnknownMethodType::kOneWay;
        break;
      case fidl::UnknownMethodType::kTwoWay:
        method_type = fidl_serversuite::wire::UnknownMethodType::kTwoWay;
        break;
    }
    auto result = reporter_->ReceivedUnknownMethod(metadata.method_ordinal, method_type);
    ZX_ASSERT(result.ok());
  }

  void set_binding_ref(fidl::ServerBindingRef<fidl_serversuite::OpenTarget> binding_ref) {
    ZX_ASSERT_MSG(!binding_ref_, "binding ref already set");
    binding_ref_ = std::move(binding_ref);
  }

 private:
  fidl::WireSyncClient<fidl_serversuite::Reporter> reporter_;
  std::optional<fidl::ServerBindingRef<fidl_serversuite::OpenTarget>> binding_ref_;
};

class RunnerServer : public fidl::WireServer<fidl_serversuite::Runner> {
 public:
  explicit RunnerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void IsTestEnabled(IsTestEnabledRequestView request,
                     IsTestEnabledCompleter::Sync& completer) override {
    bool is_enabled = [&request]() {
      switch (request->test) {
        case fidl_serversuite::Test::kOneWayWithNonZeroTxid:
        case fidl_serversuite::Test::kTwoWayNoPayloadWithZeroTxid:
          return false;
        default:
          return true;
      }
    }();
    completer.Reply(is_enabled);
  }

  void Start(StartRequestView request, StartCompleter::Sync& completer) override {
    std::cout << "Runner.Start()" << std::endl;

    switch (request->target.Which()) {
      case ::fidl_serversuite::wire::AnyTarget::Tag::kClosedTarget: {
        auto target_server = std::make_unique<ClosedTargetServer>(std::move(request->reporter));
        fidl::BindServer(dispatcher_, std::move(request->target.closed_target()),
                         std::move(target_server), [](auto*, fidl::UnbindInfo info, auto) {
                           if (!info.is_dispatcher_shutdown() && !info.is_user_initiated() &&
                               !info.is_peer_closed()) {
                             std::cout
                                 << "ClosedTarget unbound with error: " << info.FormatDescription()
                                 << std::endl;
                           }
                         });
        completer.Reply();
        break;
      }
      case ::fidl_serversuite::wire::AnyTarget::Tag::kAjarTarget: {
        auto target_server = std::make_unique<AjarTargetServer>(std::move(request->reporter));
        fidl::BindServer(dispatcher_, std::move(request->target.ajar_target()),
                         std::move(target_server), [](auto*, fidl::UnbindInfo info, auto) {
                           if (!info.is_dispatcher_shutdown() && !info.is_user_initiated() &&
                               !info.is_peer_closed()) {
                             std::cout
                                 << "AjarTarget unbound with error: " << info.FormatDescription()
                                 << std::endl;
                           }
                         });
        completer.Reply();
        break;
      }
      case ::fidl_serversuite::wire::AnyTarget::Tag::kOpenTarget: {
        auto target_server = std::make_shared<OpenTargetServer>(std::move(request->reporter));
        auto binding_ref = fidl::BindServer(
            dispatcher_, std::move(request->target.open_target()), target_server,
            [](auto*, fidl::UnbindInfo info, auto) {
              if (!info.is_dispatcher_shutdown() && !info.is_user_initiated() &&
                  !info.is_peer_closed()) {
                std::cout << "OpenTarget unbound with error: " << info.FormatDescription()
                          << std::endl;
              }
            });
        // This is thread safe because the new server runs in the same
        // dispatcher thread as the request to start it.
        target_server->set_binding_ref(std::move(binding_ref));
        completer.Reply();
        break;
      }
    }
  }

  void CheckAlive(CheckAliveCompleter::Sync& completer) override { completer.Reply(); }

 private:
  async_dispatcher_t* dispatcher_;
};

int main(int argc, const char** argv) {
  std::cout << "LLCPP server: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  RunnerServer runner_server(loop.dispatcher());
  auto result = outgoing.AddProtocol<fidl_serversuite::Runner>(&runner_server);
  ZX_ASSERT(result.is_ok());

  std::cout << "LLCPP server: ready!" << std::endl;
  return loop.Run();
}
