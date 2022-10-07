// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.clientsuite/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

#include "src/tests/fidl/client_suite/cpp_util/error_util.h"

class RunnerServer : public fidl::Server<fidl_clientsuite::Runner> {
 public:
  RunnerServer() = default;

  void IsTestEnabled(IsTestEnabledRequest& request,
                     IsTestEnabledCompleter::Sync& completer) override {
    completer.Reply(true);
  }

  void CheckAlive(CheckAliveCompleter::Sync& completer) override { completer.Reply(); }

  void CallTwoWayNoPayload(CallTwoWayNoPayloadRequest& request,
                           CallTwoWayNoPayloadCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->TwoWayNoPayload();
    if (result.is_ok()) {
      completer.Reply(
          fidl_clientsuite::EmptyResultClassification::WithSuccess(::fidl_clientsuite::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error_value())));
    }
  }

  void CallStrictOneWay(CallStrictOneWayRequest& request,
                        CallStrictOneWayCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->StrictOneWay();
    if (result.is_ok()) {
      completer.Reply(
          fidl_clientsuite::EmptyResultClassification::WithSuccess(::fidl_clientsuite::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error_value())));
    }
  }

  void CallFlexibleOneWay(CallFlexibleOneWayRequest& request,
                          CallFlexibleOneWayCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->FlexibleOneWay();
    if (result.is_ok()) {
      completer.Reply(
          fidl_clientsuite::EmptyResultClassification::WithSuccess(::fidl_clientsuite::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error_value())));
    }
  }

  void CallStrictTwoWay(CallStrictTwoWayRequest& request,
                        CallStrictTwoWayCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->StrictTwoWay();
    if (result.is_ok()) {
      completer.Reply(
          fidl_clientsuite::EmptyResultClassification::WithSuccess(::fidl_clientsuite::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error_value())));
    }
  }

  void CallStrictTwoWayErr(CallStrictTwoWayErrRequest& request,
                           CallStrictTwoWayErrCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->StrictTwoWayErr();
    if (result.is_ok()) {
      completer.Reply(fidl_clientsuite::EmptyResultWithErrorClassification::WithSuccess(
          ::fidl_clientsuite::Empty()));
    } else if (result.error_value().is_domain_error()) {
      completer.Reply(fidl_clientsuite::EmptyResultWithErrorClassification::WithApplicationError(
          result.error_value().domain_error()));
    } else {
      completer.Reply(fidl_clientsuite::EmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error_value().framework_error())));
    }
  }

  void CallFlexibleTwoWay(CallFlexibleTwoWayRequest& request,
                          CallFlexibleTwoWayCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->FlexibleTwoWay();
    if (result.is_ok()) {
      completer.Reply(
          fidl_clientsuite::EmptyResultClassification::WithSuccess(::fidl_clientsuite::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error_value())));
    }
  }

  void CallFlexibleTwoWayFields(CallFlexibleTwoWayFieldsRequest& request,
                                CallFlexibleTwoWayFieldsCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->FlexibleTwoWayFields();
    if (result.is_ok()) {
      completer.Reply(
          fidl::Response<fidl_clientsuite::Runner::CallFlexibleTwoWayFields>::WithSuccess(
              result.value()));
    } else {
      completer.Reply(
          fidl::Response<fidl_clientsuite::Runner::CallFlexibleTwoWayFields>::WithFidlError(
              clienttest_util::ClassifyError(result.error_value())));
    }
  }

  void CallFlexibleTwoWayErr(CallFlexibleTwoWayErrRequest& request,
                             CallFlexibleTwoWayErrCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->FlexibleTwoWayErr();
    if (result.is_ok()) {
      completer.Reply(fidl_clientsuite::EmptyResultWithErrorClassification::WithSuccess(
          ::fidl_clientsuite::Empty()));
    } else if (result.error_value().is_domain_error()) {
      completer.Reply(fidl_clientsuite::EmptyResultWithErrorClassification::WithApplicationError(
          result.error_value().domain_error()));
    } else {
      completer.Reply(fidl_clientsuite::EmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error_value().framework_error())));
    }
  }

  void CallFlexibleTwoWayFieldsErr(CallFlexibleTwoWayFieldsErrRequest& request,
                                   CallFlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
    auto client = fidl::SyncClient(std::move(request.target()));
    auto result = client->FlexibleTwoWayFieldsErr();
    if (result.is_ok()) {
      completer.Reply(
          fidl::Response<fidl_clientsuite::Runner::CallFlexibleTwoWayFieldsErr>::WithSuccess(
              result.value()));
    } else if (result.error_value().is_domain_error()) {
      completer.Reply(fidl::Response<fidl_clientsuite::Runner::CallFlexibleTwoWayFieldsErr>::
                          WithApplicationError(result.error_value().domain_error()));
    } else {
      completer.Reply(
          fidl::Response<fidl_clientsuite::Runner::CallFlexibleTwoWayFieldsErr>::WithFidlError(
              clienttest_util::ClassifyError(result.error_value().framework_error())));
    }
  }

  void ReceiveClosedEvents(ReceiveClosedEventsRequest& request,
                           ReceiveClosedEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::SyncEventHandler<fidl_clientsuite::ClosedTarget> {
     public:
      std::optional<fidl_clientsuite::ClosedTargetEventReport> received_event;
    };

    std::thread([client = fidl::SyncClient(std::move(request.target())),
                 reporter = fidl::SyncClient(std::move(request.reporter()))]() mutable {
      while (true) {
        EventHandler event_handler;
        auto handle_result = client.HandleOneEvent(event_handler);
        if (!handle_result.ok()) {
          event_handler.received_event = fidl_clientsuite::ClosedTargetEventReport::WithFidlError(
              clienttest_util::ClassifyError(handle_result));
        }
        ZX_ASSERT(event_handler.received_event.has_value());
        auto report_result = reporter->ReportEvent(event_handler.received_event.value());
        if (report_result.is_error()) {
          // If the harness has closed the reporter, we can stop reporting events.
          if (report_result.error_value().is_peer_closed()) {
            break;
          }
          ZX_PANIC("Could not report received event: %s",
                   report_result.error_value().lossy_description());
        }

        // If we got any error other than UnknownInteraction, we can stop
        // reporting events. However we want to keep the thread open until the
        // reporter closes so that the test case has the ability to
        // differentiate between the channel being closed due to the error vs
        // due to the channel going out of scope.
        if (!handle_result.ok() && handle_result.reason() != fidl::Reason::kUnknownMethod) {
          auto wait_status = reporter.client_end().channel().wait_one(
              ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
          ZX_ASSERT(wait_status == ZX_OK);
          break;
        }
      }
    }).detach();
    completer.Reply();
  }

  void ReceiveAjarEvents(ReceiveAjarEventsRequest& request,
                         ReceiveAjarEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::SyncEventHandler<fidl_clientsuite::AjarTarget> {
      void handle_unknown_event(
          fidl::UnknownEventMetadata<fidl_clientsuite::AjarTarget> metadata) override {
        ZX_ASSERT(!received_event.has_value());
        received_event = fidl_clientsuite::AjarTargetEventReport::WithUnknownEvent(
            {{.ordinal = metadata.method_ordinal}});
      }

     public:
      std::optional<fidl_clientsuite::AjarTargetEventReport> received_event;
    };

    std::thread([client = fidl::SyncClient(std::move(request.target())),
                 reporter = fidl::SyncClient(std::move(request.reporter()))]() mutable {
      while (true) {
        EventHandler event_handler;
        auto handle_result = client.HandleOneEvent(event_handler);
        if (!handle_result.ok()) {
          event_handler.received_event = fidl_clientsuite::AjarTargetEventReport::WithFidlError(
              clienttest_util::ClassifyError(handle_result));
        }
        ZX_ASSERT(event_handler.received_event.has_value());
        auto report_result = reporter->ReportEvent(event_handler.received_event.value());
        if (report_result.is_error()) {
          // If the harness has closed the reporter, we can stop reporting events.
          if (report_result.error_value().is_peer_closed()) {
            break;
          }
          ZX_PANIC("Could not report received event: %s",
                   report_result.error_value().lossy_description());
        }

        // If we got any error other than UnknownInteraction, we can stop
        // reporting events. However we want to keep the thread open until the
        // reporter closes so that the test case has the ability to
        // differentiate between the channel being closed due to the error vs
        // due to the channel going out of scope.
        if (!handle_result.ok() && handle_result.reason() != fidl::Reason::kUnknownMethod) {
          auto wait_status = reporter.client_end().channel().wait_one(
              ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
          ZX_ASSERT(wait_status == ZX_OK);
          break;
        }
      }
    }).detach();
    completer.Reply();
  }

  void ReceiveOpenEvents(ReceiveOpenEventsRequest& request,
                         ReceiveOpenEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::SyncEventHandler<fidl_clientsuite::OpenTarget> {
      void StrictEvent(fidl::Event<fidl_clientsuite::OpenTarget::StrictEvent>& event) override {
        ZX_ASSERT(!received_event.has_value());
        received_event = fidl_clientsuite::OpenTargetEventReport::WithStrictEvent({});
      }

      void FlexibleEvent(fidl::Event<fidl_clientsuite::OpenTarget::FlexibleEvent>& event) override {
        ZX_ASSERT(!received_event.has_value());
        received_event = fidl_clientsuite::OpenTargetEventReport::WithFlexibleEvent({});
      }

      void handle_unknown_event(
          fidl::UnknownEventMetadata<fidl_clientsuite::OpenTarget> metadata) override {
        ZX_ASSERT(!received_event.has_value());
        received_event = fidl_clientsuite::OpenTargetEventReport::WithUnknownEvent(
            {{.ordinal = metadata.method_ordinal}});
      }

     public:
      std::optional<fidl_clientsuite::OpenTargetEventReport> received_event;
    };

    std::thread([client = fidl::SyncClient(std::move(request.target())),
                 reporter = fidl::SyncClient(std::move(request.reporter()))]() mutable {
      while (true) {
        EventHandler event_handler;
        auto handle_result = client.HandleOneEvent(event_handler);
        if (!handle_result.ok()) {
          event_handler.received_event = fidl_clientsuite::OpenTargetEventReport::WithFidlError(
              clienttest_util::ClassifyError(handle_result));
        }
        ZX_ASSERT(event_handler.received_event.has_value());
        auto report_result = reporter->ReportEvent(event_handler.received_event.value());
        if (report_result.is_error()) {
          // If the harness has closed the reporter, we can stop reporting events.
          if (report_result.error_value().is_peer_closed()) {
            break;
          }
          ZX_PANIC("Could not report received event: %s",
                   report_result.error_value().lossy_description());
        }

        // If we got any error other than UnknownInteraction, we can stop
        // reporting events. However we want to keep the thread open until the
        // reporter closes so that the test case has the ability to
        // differentiate between the channel being closed due to the error vs
        // due to the channel going out of scope.
        if (!handle_result.ok() && handle_result.reason() != fidl::Reason::kUnknownMethod) {
          auto wait_status = reporter.client_end().channel().wait_one(
              ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr);
          ZX_ASSERT(wait_status == ZX_OK);
          break;
        }
      }
    }).detach();
    completer.Reply();
  }
};

int main(int argc, const char** argv) {
  std::cout << "CPP sync client: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  RunnerServer runner_server;
  auto result = outgoing.AddProtocol<fidl_clientsuite::Runner>(&runner_server);
  ZX_ASSERT(result.is_ok());

  std::cout << "CPP sync client: ready!" << std::endl;
  return loop.Run();
}
