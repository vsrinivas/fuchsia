// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.clientsuite/cpp/fidl.h>
#include <fidl/fidl.clientsuite/cpp/markers.h>
#include <fidl/fidl.clientsuite/cpp/natural_types.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/client.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/status.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <iostream>
#include <memory>

#include "src/tests/fidl/client_suite/cpp_util/error_util.h"

class RunnerServer : public fidl::Server<fidl_clientsuite::Runner> {
 public:
  explicit RunnerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void IsTestEnabled(IsTestEnabledRequest& request,
                     IsTestEnabledCompleter::Sync& completer) override {
    completer.Reply(true);
  }

  void CheckAlive(CheckAliveRequest& request, CheckAliveCompleter::Sync& completer) override {
    completer.Reply();
  }

  void CallTwoWayNoPayload(CallTwoWayNoPayloadRequest& request,
                           CallTwoWayNoPayloadCompleter::Sync& completer) override {
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
    client->TwoWayNoPayload().ThenExactlyOnce([completer = completer.ToAsync(),
                                               client = client.Clone()](auto result) mutable {
      if (result.is_ok()) {
        completer.Reply(
            fidl_clientsuite::EmptyResultClassification::WithSuccess(::fidl_clientsuite::Empty()));
      } else {
        completer.Reply(fidl_clientsuite::EmptyResultClassification::WithFidlError(
            clienttest_util::ClassifyError(result.error_value())));
      }
    });
  }

  void CallStrictOneWay(CallStrictOneWayRequest& request,
                        CallStrictOneWayCompleter::Sync& completer) override {
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
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
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
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
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
    client->StrictTwoWay().ThenExactlyOnce([completer = completer.ToAsync(),
                                            client = client.Clone()](auto result) mutable {
      if (result.is_ok()) {
        completer.Reply(
            fidl_clientsuite::EmptyResultClassification::WithSuccess(::fidl_clientsuite::Empty()));
      } else {
        completer.Reply(fidl_clientsuite::EmptyResultClassification::WithFidlError(
            clienttest_util::ClassifyError(result.error_value())));
      }
    });
  }

  void CallStrictTwoWayErr(CallStrictTwoWayErrRequest& request,
                           CallStrictTwoWayErrCompleter::Sync& completer) override {
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
    client->StrictTwoWayErr().ThenExactlyOnce([completer = completer.ToAsync(),
                                               client = client.Clone()](auto result) mutable {
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
    });
  }

  void CallFlexibleTwoWay(CallFlexibleTwoWayRequest& request,
                          CallFlexibleTwoWayCompleter::Sync& completer) override {
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
    client->FlexibleTwoWay().ThenExactlyOnce([completer = completer.ToAsync(),
                                              client = client.Clone()](auto result) mutable {
      if (result.is_ok()) {
        completer.Reply(
            fidl_clientsuite::EmptyResultClassification::WithSuccess(::fidl_clientsuite::Empty()));
      } else {
        completer.Reply(fidl_clientsuite::EmptyResultClassification::WithFidlError(
            clienttest_util::ClassifyError(result.error_value())));
      }
    });
  }

  void CallFlexibleTwoWayFields(CallFlexibleTwoWayFieldsRequest& request,
                                CallFlexibleTwoWayFieldsCompleter::Sync& completer) override {
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
    client->FlexibleTwoWayFields().ThenExactlyOnce(
        [completer = completer.ToAsync(), client = client.Clone()](auto& result) mutable {
          if (result.is_ok()) {
            completer.Reply(
                fidl::Response<fidl_clientsuite::Runner::CallFlexibleTwoWayFields>::WithSuccess(
                    result.value()));
          } else {
            completer.Reply(
                fidl::Response<fidl_clientsuite::Runner::CallFlexibleTwoWayFields>::WithFidlError(
                    clienttest_util::ClassifyError(result.error_value())));
          }
        });
  }

  void CallFlexibleTwoWayErr(CallFlexibleTwoWayErrRequest& request,
                             CallFlexibleTwoWayErrCompleter::Sync& completer) override {
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
    client->FlexibleTwoWayErr().ThenExactlyOnce([completer = completer.ToAsync(),
                                                 client = client.Clone()](auto& result) mutable {
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
    });
  }

  void CallFlexibleTwoWayFieldsErr(CallFlexibleTwoWayFieldsErrRequest& request,
                                   CallFlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_);
    client->FlexibleTwoWayFieldsErr().ThenExactlyOnce([completer = completer.ToAsync(),
                                                       client =
                                                           client.Clone()](auto& result) mutable {
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
    });
  }

  void ReceiveClosedEvents(ReceiveClosedEventsRequest& request,
                           ReceiveClosedEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::AsyncEventHandler<fidl_clientsuite::ClosedTarget> {
     public:
      explicit EventHandler(async_dispatcher_t* dispatcher,
                            fidl::ClientEnd<fidl_clientsuite::ClosedTargetEventReporter> reporter)
          : dispatcher_(dispatcher), reporter_(std::move(reporter)) {}

      void on_fidl_error(fidl::UnbindInfo error) override {
        auto report_result =
            reporter_->ReportEvent(fidl_clientsuite::ClosedTargetEventReport::WithFidlError(
                clienttest_util::ClassifyError(error.ToError())));
        if (report_result.is_error()) {
          if (report_result.error_value().is_peer_closed()) {
            client_.AsyncTeardown();
          }
          ZX_PANIC("Could not report received event: %s",
                   report_result.error_value().lossy_description());
        } else {
          auto waiter = std::make_unique<async::WaitOnce>(reporter_.client_end().channel().get(),
                                                          ZX_CHANNEL_PEER_CLOSED);
          auto waiter_ptr = waiter.get();
          waiter_ptr->Begin(
              dispatcher_,
              [client = std::move(client_), reporter = std::move(reporter_),
               waiter = std::move(waiter)](auto* dispatcher, auto* wait, auto status,
                                           auto* signal) mutable { client.AsyncTeardown(); });
        }
      }

      void SetClient(fidl::SharedClient<fidl_clientsuite::ClosedTarget> client) {
        client_ = std::move(client);
      }

     private:
      async_dispatcher_t* dispatcher_;
      fidl::SharedClient<fidl_clientsuite::ClosedTarget> client_;
      fidl::SyncClient<fidl_clientsuite::ClosedTargetEventReporter> reporter_;
    };

    auto handler = std::make_unique<EventHandler>(dispatcher_, std::move(request.reporter()));
    auto* handler_ptr = handler.get();
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_, std::move(handler));
    handler_ptr->SetClient(std::move(client));
    completer.Reply();
  }

  void ReceiveAjarEvents(ReceiveAjarEventsRequest& request,
                         ReceiveAjarEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::AsyncEventHandler<fidl_clientsuite::AjarTarget> {
     public:
      explicit EventHandler(async_dispatcher_t* dispatcher,
                            fidl::ClientEnd<fidl_clientsuite::AjarTargetEventReporter> reporter)
          : dispatcher_(dispatcher), reporter_(std::move(reporter)) {}

      // Report an event to the harness. If the reporter is closed, return
      // false. If reporting succeeded, returns true.
      bool ReportEvent(fidl_clientsuite::AjarTargetEventReport event) {
        auto report_result = reporter_->ReportEvent(std::move(event));
        if (report_result.is_error()) {
          if (report_result.error_value().is_peer_closed()) {
            client_.AsyncTeardown();
            return false;
          }
          ZX_PANIC("Could not report received event: %s",
                   report_result.error_value().lossy_description());
        }
        return true;
      }

      void handle_unknown_event(
          fidl::UnknownEventMetadata<fidl_clientsuite::AjarTarget> metadata) override {
        ReportEvent(fidl_clientsuite::AjarTargetEventReport::WithUnknownEvent(
            {{.ordinal = metadata.method_ordinal}}));
      }

      void on_fidl_error(fidl::UnbindInfo error) override {
        if (ReportEvent(fidl_clientsuite::AjarTargetEventReport::WithFidlError(
                clienttest_util::ClassifyError(error.ToError())))) {
          auto waiter = std::make_unique<async::WaitOnce>(reporter_.client_end().channel().get(),
                                                          ZX_CHANNEL_PEER_CLOSED);
          auto waiter_ptr = waiter.get();
          waiter_ptr->Begin(
              dispatcher_,
              [client = std::move(client_), reporter = std::move(reporter_),
               waiter = std::move(waiter)](auto* dispatcher, auto* wait, auto status,
                                           auto* signal) mutable { client.AsyncTeardown(); });
        }
      }

      void SetClient(fidl::SharedClient<fidl_clientsuite::AjarTarget> client) {
        client_ = std::move(client);
      }

     private:
      async_dispatcher_t* dispatcher_;
      fidl::SharedClient<fidl_clientsuite::AjarTarget> client_;
      fidl::SyncClient<fidl_clientsuite::AjarTargetEventReporter> reporter_;
    };

    auto handler = std::make_unique<EventHandler>(dispatcher_, std::move(request.reporter()));
    auto* handler_ptr = handler.get();
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_, std::move(handler));
    handler_ptr->SetClient(std::move(client));
    completer.Reply();
  }

  void ReceiveOpenEvents(ReceiveOpenEventsRequest& request,
                         ReceiveOpenEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::AsyncEventHandler<fidl_clientsuite::OpenTarget> {
     public:
      explicit EventHandler(async_dispatcher_t* dispatcher,
                            fidl::ClientEnd<fidl_clientsuite::OpenTargetEventReporter> reporter)
          : dispatcher_(dispatcher), reporter_(std::move(reporter)) {}

      // Report an event to the harness. If the reporter is closed, return
      // false. If reporting succeeded, returns true.
      bool ReportEvent(fidl_clientsuite::OpenTargetEventReport event) {
        auto report_result = reporter_->ReportEvent(std::move(event));
        if (report_result.is_error()) {
          if (report_result.error_value().is_peer_closed()) {
            client_.AsyncTeardown();
            return false;
          }
          ZX_PANIC("Could not report received event: %s",
                   report_result.error_value().lossy_description());
        }
        return true;
      }

      void StrictEvent(fidl::Event<fidl_clientsuite::OpenTarget::StrictEvent>& event) override {
        ReportEvent(fidl_clientsuite::OpenTargetEventReport::WithStrictEvent({}));
      }

      void FlexibleEvent(fidl::Event<fidl_clientsuite::OpenTarget::FlexibleEvent>& event) override {
        ReportEvent(fidl_clientsuite::OpenTargetEventReport::WithFlexibleEvent({}));
      }

      void handle_unknown_event(
          fidl::UnknownEventMetadata<fidl_clientsuite::OpenTarget> metadata) override {
        ReportEvent(fidl_clientsuite::OpenTargetEventReport::WithUnknownEvent(
            {{.ordinal = metadata.method_ordinal}}));
      }

      void on_fidl_error(fidl::UnbindInfo error) override {
        if (ReportEvent(fidl_clientsuite::OpenTargetEventReport::WithFidlError(
                clienttest_util::ClassifyError(error.ToError())))) {
          auto waiter = std::make_unique<async::WaitOnce>(reporter_.client_end().channel().get(),
                                                          ZX_CHANNEL_PEER_CLOSED);
          auto waiter_ptr = waiter.get();
          waiter_ptr->Begin(
              dispatcher_,
              [client = std::move(client_), reporter = std::move(reporter_),
               waiter = std::move(waiter)](auto* dispatcher, auto* wait, auto status,
                                           auto* signal) mutable { client.AsyncTeardown(); });
        }
      }

      void SetClient(fidl::SharedClient<fidl_clientsuite::OpenTarget> client) {
        client_ = std::move(client);
      }

     private:
      async_dispatcher_t* dispatcher_;
      fidl::SharedClient<fidl_clientsuite::OpenTarget> client_;
      fidl::SyncClient<fidl_clientsuite::OpenTargetEventReporter> reporter_;
    };

    auto handler = std::make_unique<EventHandler>(dispatcher_, std::move(request.reporter()));
    auto* handler_ptr = handler.get();
    auto client = fidl::SharedClient(std::move(request.target()), dispatcher_, std::move(handler));
    handler_ptr->SetClient(std::move(client));
    completer.Reply();
  }

 private:
  async_dispatcher_t* dispatcher_;
};

int main(int argc, const char** argv) {
  std::cout << "CPP async client: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  RunnerServer runner_server(loop.dispatcher());
  auto result = outgoing.AddProtocol<fidl_clientsuite::Runner>(&runner_server);
  ZX_ASSERT(result.is_ok());

  std::cout << "CPP async client: ready!" << std::endl;
  return loop.Run();
}
