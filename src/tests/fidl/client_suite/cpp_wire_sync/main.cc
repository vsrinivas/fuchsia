// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.clientsuite/cpp/wire_messaging.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

#include "fidl/fidl.clientsuite/cpp/wire_types.h"
#include "src/tests/fidl/client_suite/cpp_util/error_util.h"

class RunnerServer : public fidl::WireServer<fidl_clientsuite::Runner> {
 public:
  RunnerServer() = default;

  void IsTestEnabled(IsTestEnabledRequestView request,
                     IsTestEnabledCompleter::Sync& completer) override {
    completer.Reply(true);
  }

  void CheckAlive(CheckAliveCompleter::Sync& completer) override { completer.Reply(); }

  void CallTwoWayNoPayload(CallTwoWayNoPayloadRequestView request,
                           CallTwoWayNoPayloadCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->TwoWayNoPayload();
    if (result.ok()) {
      completer.Reply(
          fidl::WireResponse<fidl_clientsuite::Runner::CallTwoWayNoPayload>::WithSuccess(
              ::fidl_clientsuite::wire::Empty()));
    } else {
      completer.Reply(
          fidl::WireResponse<fidl_clientsuite::Runner::CallTwoWayNoPayload>::WithFidlError(
              clienttest_util::ClassifyError(result)));
    }
  }

  void CallStrictOneWay(CallStrictOneWayRequestView request,
                        CallStrictOneWayCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->StrictOneWay();
    if (result.ok()) {
      completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithSuccess(
          ::fidl_clientsuite::wire::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error())));
    }
  }

  void CallFlexibleOneWay(CallFlexibleOneWayRequestView request,
                          CallFlexibleOneWayCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->FlexibleOneWay();
    if (result.ok()) {
      completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithSuccess(
          ::fidl_clientsuite::wire::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error())));
    }
  }

  void CallStrictTwoWay(CallStrictTwoWayRequestView request,
                        CallStrictTwoWayCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->StrictTwoWay();
    if (result.ok()) {
      completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithSuccess(
          ::fidl_clientsuite::wire::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result.error())));
    }
  }

  void CallStrictTwoWayFields(CallStrictTwoWayFieldsRequestView request,
                              CallStrictTwoWayFieldsCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->StrictTwoWayFields();
    if (result.ok()) {
      completer.Reply(
          fidl_clientsuite::wire::NonEmptyResultClassification::WithSuccess(result.value()));
    } else {
      completer.Reply(fidl_clientsuite::wire::NonEmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result)));
    }
  }

  void CallStrictTwoWayErr(CallStrictTwoWayErrRequestView request,
                           CallStrictTwoWayErrCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->StrictTwoWayErr();
    if (result.ok()) {
      if (result.value().is_ok()) {
        completer.Reply(fidl_clientsuite::wire::EmptyResultWithErrorClassification::WithSuccess(
            ::fidl_clientsuite::wire::Empty()));
      } else {
        completer.Reply(
            fidl_clientsuite::wire::EmptyResultWithErrorClassification::WithApplicationError(
                result.value().error_value()));
      }
    } else {
      completer.Reply(fidl_clientsuite::wire::EmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(result)));
    }
  }

  void CallStrictTwoWayFieldsErr(CallStrictTwoWayFieldsErrRequestView request,
                                 CallStrictTwoWayFieldsErrCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->StrictTwoWayFieldsErr();
    if (result.ok()) {
      if (result.value().is_ok()) {
        completer.Reply(fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithSuccess(
            *result.value().value()));
      } else {
        completer.Reply(
            fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithApplicationError(
                result.value().error_value()));
      }
    } else {
      completer.Reply(fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(result)));
    }
  }

  void CallFlexibleTwoWay(CallFlexibleTwoWayRequestView request,
                          CallFlexibleTwoWayCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->FlexibleTwoWay();
    if (result.ok()) {
      completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithSuccess(
          ::fidl_clientsuite::wire::Empty()));
    } else {
      completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result)));
    }
  }

  void CallFlexibleTwoWayFields(CallFlexibleTwoWayFieldsRequestView request,
                                CallFlexibleTwoWayFieldsCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->FlexibleTwoWayFields();
    if (result.ok()) {
      completer.Reply(
          fidl_clientsuite::wire::NonEmptyResultClassification::WithSuccess(result.value()));
    } else {
      completer.Reply(fidl_clientsuite::wire::NonEmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(result)));
    }
  }

  void CallFlexibleTwoWayErr(CallFlexibleTwoWayErrRequestView request,
                             CallFlexibleTwoWayErrCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->FlexibleTwoWayErr();
    if (result.ok()) {
      if (result.value().is_ok()) {
        completer.Reply(fidl_clientsuite::wire::EmptyResultWithErrorClassification::WithSuccess(
            ::fidl_clientsuite::wire::Empty()));
      } else {
        completer.Reply(
            fidl_clientsuite::wire::EmptyResultWithErrorClassification::WithApplicationError(
                result.value().error_value()));
      }
    } else {
      completer.Reply(fidl_clientsuite::wire::EmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(result)));
    }
  }

  void CallFlexibleTwoWayFieldsErr(CallFlexibleTwoWayFieldsErrRequestView request,
                                   CallFlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
    fidl::WireSyncClient client{std::move(request->target)};
    auto result = client->FlexibleTwoWayFieldsErr();
    if (result.ok()) {
      if (result.value().is_ok()) {
        completer.Reply(fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithSuccess(
            *result.value().value()));
      } else {
        completer.Reply(
            fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithApplicationError(
                result.value().error_value()));
      }
    } else {
      completer.Reply(fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(result)));
    }
  }

  void ReceiveClosedEvents(ReceiveClosedEventsRequestView request,
                           ReceiveClosedEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::WireSyncEventHandler<fidl_clientsuite::ClosedTarget> {
     public:
      // Using the natural types for the reporter makes the ajar and open
      // targets simpler to test, because |fidl_clientsuite::UnknownEvent| is >8
      // bytes, so is out-of-line and requires an ObjectView in wire types. The
      // reporter isn't under test, so using natural types for it doesn't change
      // which system is under test.
      std::optional<fidl_clientsuite::ClosedTargetEventReport> received_event;
    };

    std::thread([client = fidl::WireSyncClient(std::move(request->target)),

                 reporter = fidl::SyncClient(std::move(request->reporter))]() mutable {
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

  void ReceiveAjarEvents(ReceiveAjarEventsRequestView request,
                         ReceiveAjarEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::WireSyncEventHandler<fidl_clientsuite::AjarTarget> {
      void handle_unknown_event(
          fidl::UnknownEventMetadata<fidl_clientsuite::AjarTarget> metadata) override {
        ZX_ASSERT(!received_event.has_value());
        received_event = fidl_clientsuite::AjarTargetEventReport::WithUnknownEvent(
            {{.ordinal = metadata.method_ordinal}});
      }

     public:
      // Using the natural types for the reporter makes the ajar and open
      // targets simpler to test, because |fidl_clientsuite::UnknownEvent| is >8
      // bytes, so is out-of-line and requires an ObjectView in wire types. The
      // reporter isn't under test, so using natural types for it doesn't change
      // which system is under test.
      std::optional<fidl_clientsuite::AjarTargetEventReport> received_event;
    };

    std::thread([client = fidl::WireSyncClient(std::move(request->target)),
                 reporter = fidl::SyncClient(std::move(request->reporter))]() mutable {
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

  void ReceiveOpenEvents(ReceiveOpenEventsRequestView request,
                         ReceiveOpenEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::WireSyncEventHandler<fidl_clientsuite::OpenTarget> {
      void StrictEvent(fidl::WireEvent<fidl_clientsuite::OpenTarget::StrictEvent>* event) override {
        ZX_ASSERT(!received_event.has_value());
        received_event = fidl_clientsuite::OpenTargetEventReport::WithStrictEvent({});
      }

      void FlexibleEvent(
          fidl::WireEvent<fidl_clientsuite::OpenTarget::FlexibleEvent>* event) override {
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
      // Using the natural types for the reporter makes the ajar and open
      // targets simpler to test, because |fidl_clientsuite::UnknownEvent| is >8
      // bytes, so is out-of-line and requires an ObjectView in wire types. The
      // reporter isn't under test, so using natural types for it doesn't change
      // which system is under test.
      std::optional<fidl_clientsuite::OpenTargetEventReport> received_event;
    };

    std::thread([client = fidl::WireSyncClient(std::move(request->target)),
                 reporter = fidl::SyncClient(std::move(request->reporter))]() mutable {
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
  std::cout << "CPP wire sync client: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  RunnerServer runner_server;
  auto result = outgoing.AddProtocol<fidl_clientsuite::Runner>(&runner_server);
  ZX_ASSERT(result.is_ok());

  std::cout << "CPP wire sync client: ready!" << std::endl;
  return loop.Run();
}
