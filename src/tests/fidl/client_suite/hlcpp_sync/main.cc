// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/clientsuite/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

#include <iostream>

#include "lib/fidl/cpp/unknown_interactions_hlcpp.h"
#include "src/tests/fidl/client_suite/hlcpp_util/error_util.h"

class RunnerServer : public fidl::clientsuite::Runner {
 public:
  RunnerServer() = default;

  void IsTestEnabled(fidl::clientsuite::Test test, IsTestEnabledCallback callback) override {
    switch (test) {
      // HLCPP Sync Client Bindings do not support events.
      case fidl::clientsuite::Test::RECEIVE_STRICT_EVENT:
      case fidl::clientsuite::Test::RECEIVE_STRICT_EVENT_MISMATCHED_STRICTNESS:
      case fidl::clientsuite::Test::RECEIVE_FLEXIBLE_EVENT:
      case fidl::clientsuite::Test::RECEIVE_FLEXIBLE_EVENT_MISMATCHED_STRICTNESS:
      case fidl::clientsuite::Test::UNKNOWN_STRICT_EVENT_OPEN_PROTOCOL:
      case fidl::clientsuite::Test::UNKNOWN_FLEXIBLE_EVENT_OPEN_PROTOCOL:
      case fidl::clientsuite::Test::UNKNOWN_STRICT_EVENT_AJAR_PROTOCOL:
      case fidl::clientsuite::Test::UNKNOWN_FLEXIBLE_EVENT_AJAR_PROTOCOL:
      case fidl::clientsuite::Test::UNKNOWN_STRICT_EVENT_CLOSED_PROTOCOL:
      case fidl::clientsuite::Test::UNKNOWN_FLEXIBLE_EVENT_CLOSED_PROTOCOL:
      case fidl::clientsuite::Test::UNKNOWN_STRICT_SERVER_INITIATED_TWO_WAY:
      case fidl::clientsuite::Test::UNKNOWN_FLEXIBLE_SERVER_INITIATED_TWO_WAY:
        callback(false);
        return;
      default:
        callback(true);
        return;
    }
  }

  void CheckAlive(CheckAliveCallback callback) override { callback(); }

  void CallTwoWayNoPayload(fidl::InterfaceHandle<fidl::clientsuite::ClosedTarget> target,
                           CallTwoWayNoPayloadCallback callback) override {
    auto client = target.BindSync();
    auto status = client->TwoWayNoPayload();
    if (status == ZX_OK) {
      callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
    } else {
      callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void CallStrictOneWay(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                        CallStrictOneWayCallback callback) override {
    auto client = target.BindSync();
    auto status = client->StrictOneWay();
    if (status == ZX_OK) {
      callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
    } else {
      callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void CallFlexibleOneWay(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                          CallFlexibleOneWayCallback callback) override {
    auto client = target.BindSync();
    auto status = client->FlexibleOneWay();
    if (status == ZX_OK) {
      callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
    } else {
      callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void CallStrictTwoWay(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                        CallStrictTwoWayCallback callback) override {
    auto client = target.BindSync();
    auto status = client->StrictTwoWay();
    if (status == ZX_OK) {
      callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
    } else {
      callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void CallStrictTwoWayErr(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                           CallStrictTwoWayErrCallback callback) override {
    auto client = target.BindSync();
    fidl::clientsuite::OpenTarget_StrictTwoWayErr_Result result;
    auto status = client->StrictTwoWayErr(&result);
    if (status == ZX_OK) {
      if (result.is_response()) {
        callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithSuccess({}));
      } else if (result.is_err()) {
        callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithApplicationError(
            std::move(result.err())));
      } else {
        ZX_PANIC("invalid tag");
      }
    } else {
      callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void CallFlexibleTwoWay(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                          CallFlexibleTwoWayCallback callback) override {
    auto client = target.BindSync();
    fidl::clientsuite::OpenTarget_FlexibleTwoWay_Result result;
    auto status = client->FlexibleTwoWay(&result);
    if (status == ZX_OK) {
      if (result.is_response()) {
        callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
      } else if (result.is_transport_err()) {
        ZX_ASSERT(result.transport_err() == fidl::TransportErr::kUnknownMethod);
        callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
            fidl::clientsuite::FidlErrorKind::UNKNOWN_METHOD));
      } else {
        ZX_PANIC("invalid tag");
      }
    } else {
      callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void CallFlexibleTwoWayFields(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                                CallFlexibleTwoWayFieldsCallback callback) override {
    auto client = target.BindSync();
    fidl::clientsuite::OpenTarget_FlexibleTwoWayFields_Result result;
    auto status = client->FlexibleTwoWayFields(&result);
    if (status == ZX_OK) {
      if (result.is_response()) {
        callback(fidl::clientsuite::RunnerCallFlexibleTwoWayFieldsResponse::WithSuccess(
            std::move(result.response())));
      } else if (result.is_transport_err()) {
        ZX_ASSERT(result.transport_err() == fidl::TransportErr::kUnknownMethod);
        callback(fidl::clientsuite::RunnerCallFlexibleTwoWayFieldsResponse::WithFidlError(
            fidl::clientsuite::FidlErrorKind::UNKNOWN_METHOD));
      } else {
        ZX_PANIC("invalid tag");
      }
    } else {
      callback(fidl::clientsuite::RunnerCallFlexibleTwoWayFieldsResponse::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void CallFlexibleTwoWayErr(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                             CallFlexibleTwoWayErrCallback callback) override {
    auto client = target.BindSync();
    fidl::clientsuite::OpenTarget_FlexibleTwoWayErr_Result result;
    auto status = client->FlexibleTwoWayErr(&result);
    if (status == ZX_OK) {
      if (result.is_response()) {
        callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithSuccess({}));
      } else if (result.is_err()) {
        callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithApplicationError(
            std::move(result.err())));
      } else if (result.is_transport_err()) {
        ZX_ASSERT(result.transport_err() == fidl::TransportErr::kUnknownMethod);
        callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithFidlError(
            fidl::clientsuite::FidlErrorKind::UNKNOWN_METHOD));
      } else {
        ZX_PANIC("invalid tag");
      }
    } else {
      callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void CallFlexibleTwoWayFieldsErr(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                                   CallFlexibleTwoWayFieldsErrCallback callback) override {
    auto client = target.BindSync();
    fidl::clientsuite::OpenTarget_FlexibleTwoWayFieldsErr_Result result;
    auto status = client->FlexibleTwoWayFieldsErr(&result);
    if (status == ZX_OK) {
      if (result.is_response()) {
        callback(fidl::clientsuite::RunnerCallFlexibleTwoWayFieldsErrResponse::WithSuccess(
            std::move(result.response())));
      } else if (result.is_err()) {
        callback(fidl::clientsuite::RunnerCallFlexibleTwoWayFieldsErrResponse::WithApplicationError(
            std::move(result.err())));
      } else if (result.is_transport_err()) {
        ZX_ASSERT(result.transport_err() == fidl::TransportErr::kUnknownMethod);
        callback(fidl::clientsuite::RunnerCallFlexibleTwoWayFieldsErrResponse::WithFidlError(
            fidl::clientsuite::FidlErrorKind::UNKNOWN_METHOD));
      } else {
        ZX_PANIC("invalid tag");
      }
    } else {
      callback(fidl::clientsuite::RunnerCallFlexibleTwoWayFieldsErrResponse::WithFidlError(
          clienttest_util::ClassifyError(status)));
    }
  }

  void ReceiveClosedEvents(
      ::fidl::InterfaceHandle<::fidl::clientsuite::ClosedTarget> target,
      ::fidl::InterfaceHandle<::fidl::clientsuite::ClosedTargetEventReporter> reporter,
      ReceiveClosedEventsCallback callback) override {
    ZX_PANIC("Events are not supported on HLCPP Sync Client Bindings");
  }

  void ReceiveAjarEvents(
      ::fidl::InterfaceHandle<::fidl::clientsuite::AjarTarget> target,
      ::fidl::InterfaceHandle<::fidl::clientsuite::AjarTargetEventReporter> reporter,
      ReceiveAjarEventsCallback callback) override {
    ZX_PANIC("Events are not supported on HLCPP Sync Client Bindings");
  }

  void ReceiveOpenEvents(
      ::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
      ::fidl::InterfaceHandle<::fidl::clientsuite::OpenTargetEventReporter> reporter,
      ReceiveOpenEventsCallback callback) override {
    ZX_PANIC("Events are not supported on HLCPP Sync Client Bindings");
  }
};

int main(int argc, const char** argv) {
  std::cout << "HLCPP sync client: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  RunnerServer server;
  fidl::Binding<fidl::clientsuite::Runner> binding(&server);
  fidl::InterfaceRequestHandler<fidl::clientsuite::Runner> handler =
      [&](fidl::InterfaceRequest<fidl::clientsuite::Runner> server_end) {
        binding.Bind(std::move(server_end));
      };
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(std::move(handler));

  std::cout << "HLCPP sync client: ready!" << std::endl;
  return loop.Run();
}
