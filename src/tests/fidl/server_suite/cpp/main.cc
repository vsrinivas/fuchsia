// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.serversuite/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

class ClosedTargetServer : public fidl::Server<fidl_serversuite::ClosedTarget> {
 public:
  explicit ClosedTargetServer(fidl::ClientEnd<fidl_serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void OneWayNoPayload(OneWayNoPayloadCompleter::Sync& completer) override {
    std::cout << "ClosedTarget.OneWayNoPayload()" << std::endl;
    auto result = reporter_->ReceivedOneWayNoPayload();
    ZX_ASSERT(result.is_ok());
  }

  void TwoWayNoPayload(TwoWayNoPayloadCompleter::Sync& completer) override {
    std::cout << "ClosedTarget.TwoWayNoPayload()" << std::endl;
    completer.Reply();
  }

  void TwoWayStructPayload(TwoWayStructPayloadRequest& request,
                           TwoWayStructPayloadCompleter::Sync& completer) override {
    std::cout << "ClosedTarget.TwoWayStructPayload()" << std::endl;
    completer.Reply(request.v());
  }

  void TwoWayTablePayload(TwoWayTablePayloadRequest& request,
                          TwoWayTablePayloadCompleter::Sync& completer) override {
    std::cout << "ClosedTarget.TwoWayTablePayload()" << std::endl;
    fidl_serversuite::ClosedTargetTwoWayTablePayloadResponse response({.v = request.v()});
    completer.Reply(response);
  }

  void TwoWayUnionPayload(TwoWayUnionPayloadRequest& request,
                          TwoWayUnionPayloadCompleter::Sync& completer) override {
    std::cout << "ClosedTarget.TwoWayUnionPayload()" << std::endl;
    ZX_ASSERT(request.v().has_value());
    completer.Reply(
        fidl_serversuite::ClosedTargetTwoWayUnionPayloadResponse::WithV(request.v().value()));
  }

  void TwoWayResult(TwoWayResultRequest& request, TwoWayResultCompleter::Sync& completer) override {
    std::cout << "ClosedTarget.TwoWayResult()" << std::endl;
    switch (request.Which()) {
      case TwoWayResultRequest::Tag::kPayload:
        completer.Reply(fit::ok(request.payload().value()));
        return;
      case TwoWayResultRequest::Tag::kError:
        completer.Reply(fit::error(request.error().value()));
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

  void ByteVectorSize(ByteVectorSizeRequest& request,
                      ByteVectorSizeCompleter::Sync& completer) override {
    completer.Reply(static_cast<uint32_t>(request.vec().size()));
  }

  void HandleVectorSize(HandleVectorSizeRequest& request,
                        HandleVectorSizeCompleter::Sync& completer) override {
    completer.Reply(static_cast<uint32_t>(request.vec().size()));
  }

  void CreateNByteVector(CreateNByteVectorRequest& request,
                         CreateNByteVectorCompleter::Sync& completer) override {
    std::vector<uint8_t> bytes(request.n());
    completer.Reply(std::move(bytes));
  }

  void CreateNHandleVector(CreateNHandleVectorRequest& request,
                           CreateNHandleVectorCompleter::Sync& completer) override {
    std::vector<zx::event> handles(request.n());
    for (auto& handle : handles) {
      ZX_ASSERT(ZX_OK == zx::event::create(0, &handle));
    }
    completer.Reply(std::move(handles));
  }

 private:
  fidl::SyncClient<fidl_serversuite::Reporter> reporter_;
};

class AjarTargetServer : public fidl::Server<fidl_serversuite::AjarTarget> {
 public:
  explicit AjarTargetServer(fidl::ClientEnd<fidl_serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void handle_unknown_method(fidl::UnknownMethodMetadata<fidl_serversuite::AjarTarget> metadata,
                             fidl::UnknownMethodCompleter::Sync& completer) override {
    auto result = reporter_->ReceivedUnknownMethod(fidl_serversuite::UnknownMethodInfo(
        metadata.method_ordinal, fidl_serversuite::UnknownMethodType::kOneWay));
    ZX_ASSERT(result.is_ok());
  }

 private:
  fidl::SyncClient<fidl_serversuite::Reporter> reporter_;
};

class OpenTargetServer : public fidl::Server<fidl_serversuite::OpenTarget> {
 public:
  explicit OpenTargetServer(fidl::ClientEnd<fidl_serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void SendEvent(SendEventRequest& request, SendEventCompleter::Sync& completer) override {
    ZX_ASSERT_MSG(binding_ref_, "missing binding ref");
    switch (request.event_type()) {
      case fidl_serversuite::EventType::kStrict: {
        auto result = fidl::SendEvent(binding_ref_.value())->StrictEvent();
        ZX_ASSERT(result.is_ok());
        break;
      }
      case fidl_serversuite::EventType::kFlexible: {
        auto result = fidl::SendEvent(binding_ref_.value())->FlexibleEvent();
        ZX_ASSERT(result.is_ok());
        break;
      }
    }
  }

  void StrictOneWay(StrictOneWayCompleter::Sync& completer) override {
    auto result = reporter_->ReceivedStrictOneWay();
    ZX_ASSERT(result.is_ok());
  }

  void FlexibleOneWay(FlexibleOneWayCompleter::Sync& completer) override {
    auto result = reporter_->ReceivedFlexibleOneWay();
    ZX_ASSERT(result.is_ok());
  }

  void StrictTwoWay(StrictTwoWayCompleter::Sync& completer) override { completer.Reply(); }

  void StrictTwoWayFields(StrictTwoWayFieldsRequest& request,
                          StrictTwoWayFieldsCompleter::Sync& completer) override {
    completer.Reply(request.reply_with());
  }

  void StrictTwoWayErr(StrictTwoWayErrRequest& request,
                       StrictTwoWayErrCompleter::Sync& completer) override {
    switch (request.Which()) {
      case fidl_serversuite::OpenTargetStrictTwoWayErrRequest::Tag::kReplySuccess:
        completer.Reply(fit::ok());
        break;
      case fidl_serversuite::OpenTargetStrictTwoWayErrRequest::Tag::kReplyError:
        completer.Reply(fit::error(request.reply_error().value()));
        break;
    }
  }

  void StrictTwoWayFieldsErr(StrictTwoWayFieldsErrRequest& request,
                             StrictTwoWayFieldsErrCompleter::Sync& completer) override {
    switch (request.Which()) {
      case fidl_serversuite::OpenTargetStrictTwoWayFieldsErrRequest::Tag::kReplySuccess:
        completer.Reply(fit::ok(request.reply_success().value()));
        break;
      case fidl_serversuite::OpenTargetStrictTwoWayFieldsErrRequest::Tag::kReplyError:
        completer.Reply(fit::error(request.reply_error().value()));
        break;
    }
  }

  void FlexibleTwoWay(FlexibleTwoWayCompleter::Sync& completer) override { completer.Reply(); }

  void FlexibleTwoWayFields(FlexibleTwoWayFieldsRequest& request,
                            FlexibleTwoWayFieldsCompleter::Sync& completer) override {
    completer.Reply(request.reply_with());
  }

  void FlexibleTwoWayErr(FlexibleTwoWayErrRequest& request,
                         FlexibleTwoWayErrCompleter::Sync& completer) override {
    switch (request.Which()) {
      case fidl_serversuite::OpenTargetFlexibleTwoWayErrRequest::Tag::kReplySuccess:
        completer.Reply(fit::ok());
        break;
      case fidl_serversuite::OpenTargetFlexibleTwoWayErrRequest::Tag::kReplyError:
        completer.Reply(fit::error(request.reply_error().value()));
        break;
    }
  }

  void FlexibleTwoWayFieldsErr(FlexibleTwoWayFieldsErrRequest& request,
                               FlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
    switch (request.Which()) {
      case fidl_serversuite::OpenTargetFlexibleTwoWayFieldsErrRequest::Tag::kReplySuccess:
        completer.Reply(fit::ok(request.reply_success().value()));
        break;
      case fidl_serversuite::OpenTargetFlexibleTwoWayFieldsErrRequest::Tag::kReplyError:
        completer.Reply(fit::error(request.reply_error().value()));
        break;
    }
  }

  void handle_unknown_method(fidl::UnknownMethodMetadata<fidl_serversuite::OpenTarget> metadata,
                             fidl::UnknownMethodCompleter::Sync& completer) override {
    fidl_serversuite::UnknownMethodType method_type;
    switch (metadata.unknown_interaction_type) {
      case fidl::UnknownMethodType::kOneWay:
        method_type = fidl_serversuite::UnknownMethodType::kOneWay;
        break;
      case fidl::UnknownMethodType::kTwoWay:
        method_type = fidl_serversuite::UnknownMethodType::kTwoWay;
        break;
    }
    auto result = reporter_->ReceivedUnknownMethod(
        fidl_serversuite::UnknownMethodInfo(metadata.method_ordinal, method_type));
    ZX_ASSERT(result.is_ok());
  }

  void set_binding_ref(fidl::ServerBindingRef<fidl_serversuite::OpenTarget> binding_ref) {
    ZX_ASSERT_MSG(!binding_ref_, "binding ref already set");
    binding_ref_ = std::move(binding_ref);
  }

 private:
  fidl::SyncClient<fidl_serversuite::Reporter> reporter_;
  std::optional<fidl::ServerBindingRef<fidl_serversuite::OpenTarget>> binding_ref_;
};

class RunnerServer : public fidl::Server<fidl_serversuite::Runner> {
 public:
  explicit RunnerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void IsTestEnabled(IsTestEnabledRequest& request,
                     IsTestEnabledCompleter::Sync& completer) override {
    bool is_enabled = [request]() {
      switch (request.test()) {
        case fidl_serversuite::Test::kIgnoreDisabled:
          // This case will forever be false, as it is intended to validate the "test disabling"
          // functionality of the runner itself.
          return false;

        case fidl_serversuite::Test::kOneWayWithNonZeroTxid:
        case fidl_serversuite::Test::kTwoWayNoPayloadWithZeroTxid:
          return false;

        case fidl_serversuite::Test::kGoodDecodeBoundedKnownSmallMessage:
        case fidl_serversuite::Test::kGoodDecodeBoundedMaybeSmallMessage:
        case fidl_serversuite::Test::kGoodDecodeBoundedMaybeLargeMessage:
        case fidl_serversuite::Test::kGoodDecodeSemiBoundedUnknowableSmallMessage:
        case fidl_serversuite::Test::kGoodDecodeSemiBoundedUnknowableLargeMessage:
        case fidl_serversuite::Test::kGoodDecodeSemiBoundedMaybeSmallMessage:
        case fidl_serversuite::Test::kGoodDecodeSemiBoundedMaybeLargeMessage:
        case fidl_serversuite::Test::kGoodDecodeUnboundedSmallMessage:
        case fidl_serversuite::Test::kGoodDecodeUnboundedLargeMessage:
        case fidl_serversuite::Test::kGoodDecode64HandleSmallMessage:
        case fidl_serversuite::Test::kGoodDecode63HandleLargeMessage:
        case fidl_serversuite::Test::kGoodDecodeUnknownSmallMessage:
        case fidl_serversuite::Test::kGoodDecodeUnknownLargeMessage:
        case fidl_serversuite::Test::kBadDecodeByteOverflowFlagSetOnSmallMessage:
        case fidl_serversuite::Test::kBadDecodeByteOverflowFlagUnsetOnLargeMessage:
        case fidl_serversuite::Test::kBadDecodeLargeMessageInfoOmitted:
        case fidl_serversuite::Test::kBadDecodeLargeMessageInfoTooSmall:
        case fidl_serversuite::Test::kBadDecodeLargeMessageInfoTooLarge:
        case fidl_serversuite::Test::kBadDecodeLargeMessageInfoTopHalfUnzeroed:
        case fidl_serversuite::Test::kBadDecodeLargeMessageInfoByteCountIsZero:
        case fidl_serversuite::Test::kBadDecodeLargeMessageInfoByteCountTooSmall:
        case fidl_serversuite::Test::kBadDecodeLargeMessageInfoByteCountNotEqualToBound:
        case fidl_serversuite::Test::kBadDecodeNoHandles:
        case fidl_serversuite::Test::kBadDecodeTooFewHandles:
        case fidl_serversuite::Test::kBadDecode64HandleLargeMessage:
        case fidl_serversuite::Test::kBadDecodeLastHandleNotVmo:
        case fidl_serversuite::Test::kBadDecodeLastHandleInsufficientRights:
        case fidl_serversuite::Test::kBadDecodeVmoTooSmall:
        case fidl_serversuite::Test::kBadDecodeVmoTooLarge:
          // TODO(fxbug.dev/114261): Test decoding large messages.
          return false;

        case fidl_serversuite::Test::kGoodEncodeBoundedKnownSmallMessage:
        case fidl_serversuite::Test::kGoodEncodeBoundedMaybeSmallMessage:
        case fidl_serversuite::Test::kGoodEncodeBoundedMaybeLargeMessage:
        case fidl_serversuite::Test::kGoodEncodeSemiBoundedKnownSmallMessage:
        case fidl_serversuite::Test::kGoodEncodeSemiBoundedMaybeSmallMessage:
        case fidl_serversuite::Test::kGoodEncodeSemiBoundedMaybeLargeMessage:
        case fidl_serversuite::Test::kGoodEncodeUnboundedSmallMessage:
        case fidl_serversuite::Test::kGoodEncodeUnboundedLargeMessage:
        case fidl_serversuite::Test::kGoodEncode64HandleSmallMessage:
        case fidl_serversuite::Test::kGoodEncode63HandleLargeMessage:
        case fidl_serversuite::Test::kBadEncode64HandleLargeMessage:
          // TODO(fxbug.dev/114263): Test encoding large messages.
          return false;

        default:
          return true;
      }
    }();
    completer.Reply(is_enabled);
  }

  void Start(StartRequest& request, StartCompleter::Sync& completer) override {
    std::cout << "Runner.Start()" << std::endl;

    switch (request.target().Which()) {
      case fidl_serversuite::AnyTarget::Tag::kClosedTarget: {
        auto target_server = std::make_unique<ClosedTargetServer>(std::move(request.reporter()));
        fidl::BindServer(dispatcher_, std::move(request.target().closed_target().value()),
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
      case fidl_serversuite::AnyTarget::Tag::kAjarTarget: {
        auto target_server = std::make_unique<AjarTargetServer>(std::move(request.reporter()));
        fidl::BindServer(dispatcher_, std::move(request.target().ajar_target().value()),
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
      case fidl_serversuite::AnyTarget::Tag::kOpenTarget: {
        auto target_server = std::make_shared<OpenTargetServer>(std::move(request.reporter()));
        auto binding_ref = fidl::BindServer(
            dispatcher_, std::move(request.target().open_target().value()), target_server,
            [](auto*, fidl::UnbindInfo info, auto) {
              if (!info.is_dispatcher_shutdown() && !info.is_user_initiated() &&
                  !info.is_peer_closed()) {
                std::cout << "OpenTarget unbound with error: " << info.FormatDescription()
                          << std::endl;
              }
            });
        // This is thread safe because the new server runs in the same
        // dispatcher thread as the request to start it.
        target_server->set_binding_ref(binding_ref);
        completer.Reply();
        break;
      }
      case fidl_serversuite::AnyTarget::Tag::kLargeMessageTarget: {
        // TODO(fxbug.dev/114261): Test decoding large messages.
        // TODO(fxbug.dev/114263): Test encoding large messages.
        ZX_PANIC("Large messages not yet supported in C++ natural bindings");
      }
    }
  }

  void CheckAlive(CheckAliveCompleter::Sync& completer) override { completer.Reply(); }

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
