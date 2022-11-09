// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.clientsuite/cpp/markers.h>
#include <fidl/fidl.clientsuite/cpp/wire_messaging.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <iostream>

#include "fidl/fidl.clientsuite/cpp/wire_types.h"
#include "src/tests/fidl/client_suite/cpp_util/error_util.h"

class RunnerServer : public fidl::WireServer<fidl_clientsuite::Runner> {
 public:
  explicit RunnerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void IsTestEnabled(IsTestEnabledRequestView request,
                     IsTestEnabledCompleter::Sync& completer) override {
    completer.Reply(true);
  }

  void CheckAlive(CheckAliveCompleter::Sync& completer) override { completer.Reply(); }

  void CallTwoWayNoPayload(CallTwoWayNoPayloadRequestView request,
                           CallTwoWayNoPayloadCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->TwoWayNoPayload().ThenExactlyOnce(
        [completer = completer.ToAsync(), client = client.Clone()](auto& result) mutable {
          if (result.ok()) {
            completer.Reply(
                fidl::WireResponse<fidl_clientsuite::Runner::CallTwoWayNoPayload>::WithSuccess(
                    ::fidl_clientsuite::wire::Empty()));
          } else {
            completer.Reply(
                fidl::WireResponse<fidl_clientsuite::Runner::CallTwoWayNoPayload>::WithFidlError(
                    clienttest_util::ClassifyError(result)));
          }
        });
  }

  void CallTwoWayStructPayload(CallTwoWayStructPayloadRequestView request,
                               CallTwoWayStructPayloadCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->TwoWayStructPayload().ThenExactlyOnce([completer = completer.ToAsync(),
                                                   client = client.Clone()](auto& result) mutable {
      if (result.ok()) {
        completer.Reply(
            fidl::WireResponse<fidl_clientsuite::Runner::CallTwoWayStructPayload>::WithSuccess(
                result.value()));
      } else {
        completer.Reply(
            fidl::WireResponse<fidl_clientsuite::Runner::CallTwoWayStructPayload>::WithFidlError(
                clienttest_util::ClassifyError(result)));
      }
    });
  }

  void CallStrictOneWay(CallStrictOneWayRequestView request,
                        CallStrictOneWayCompleter::Sync& completer) override {
    auto client = fidl::WireClient(std::move(request->target), dispatcher_);
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
    auto client = fidl::WireClient(std::move(request->target), dispatcher_);
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
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->StrictTwoWay().ThenExactlyOnce(
        [completer = completer.ToAsync(), client = client.Clone()](auto& result) mutable {
          if (result.ok()) {
            completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithSuccess(
                ::fidl_clientsuite::wire::Empty()));
          } else {
            completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithFidlError(
                clienttest_util::ClassifyError(result.error())));
          }
        });
  }

  void CallStrictTwoWayFields(CallStrictTwoWayFieldsRequestView request,
                              CallStrictTwoWayFieldsCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->StrictTwoWayFields().ThenExactlyOnce(
        [completer = completer.ToAsync(), client = client.Clone()](auto& result) mutable {
          if (result.ok()) {
            completer.Reply(
                fidl_clientsuite::wire::NonEmptyResultClassification::WithSuccess(result.value()));
          } else {
            completer.Reply(fidl_clientsuite::wire::NonEmptyResultClassification::WithFidlError(
                clienttest_util::ClassifyError(result)));
          }
        });
  }

  void CallStrictTwoWayErr(CallStrictTwoWayErrRequestView request,
                           CallStrictTwoWayErrCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->StrictTwoWayErr().ThenExactlyOnce([completer = completer.ToAsync(),
                                               client = client.Clone()](auto& result) mutable {
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
    });
  }

  void CallStrictTwoWayFieldsErr(CallStrictTwoWayFieldsErrRequestView request,
                                 CallStrictTwoWayFieldsErrCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->StrictTwoWayFieldsErr().ThenExactlyOnce([completer = completer.ToAsync(),
                                                     client =
                                                         client.Clone()](auto& result) mutable {
      if (result.ok()) {
        if (result.value().is_ok()) {
          completer.Reply(
              fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithSuccess(
                  *result.value().value()));
        } else {
          completer.Reply(
              fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithApplicationError(
                  result.value().error_value()));
        }
      } else {
        completer.Reply(
            fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithFidlError(
                clienttest_util::ClassifyError(result)));
      }
    });
  }

  void CallFlexibleTwoWay(CallFlexibleTwoWayRequestView request,
                          CallFlexibleTwoWayCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->FlexibleTwoWay().ThenExactlyOnce(
        [completer = completer.ToAsync(), client = client.Clone()](auto& result) mutable {
          if (result.ok()) {
            completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithSuccess(
                ::fidl_clientsuite::wire::Empty()));
          } else {
            completer.Reply(fidl_clientsuite::wire::EmptyResultClassification::WithFidlError(
                clienttest_util::ClassifyError(result)));
          }
        });
  }

  void CallFlexibleTwoWayFields(CallFlexibleTwoWayFieldsRequestView request,
                                CallFlexibleTwoWayFieldsCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->FlexibleTwoWayFields().ThenExactlyOnce(
        [completer = completer.ToAsync(), client = client.Clone()](auto& result) mutable {
          if (result.ok()) {
            completer.Reply(
                fidl_clientsuite::wire::NonEmptyResultClassification::WithSuccess(result.value()));
          } else {
            completer.Reply(fidl_clientsuite::wire::NonEmptyResultClassification::WithFidlError(
                clienttest_util::ClassifyError(result)));
          }
        });
  }

  void CallFlexibleTwoWayErr(CallFlexibleTwoWayErrRequestView request,
                             CallFlexibleTwoWayErrCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->FlexibleTwoWayErr().ThenExactlyOnce([completer = completer.ToAsync(),
                                                 client = client.Clone()](auto& result) mutable {
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
    });
  }

  void CallFlexibleTwoWayFieldsErr(CallFlexibleTwoWayFieldsErrRequestView request,
                                   CallFlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
    auto client = fidl::WireSharedClient(std::move(request->target), dispatcher_);
    client->FlexibleTwoWayFieldsErr().ThenExactlyOnce([completer = completer.ToAsync(),
                                                       client =
                                                           client.Clone()](auto& result) mutable {
      if (result.ok()) {
        if (result.value().is_ok()) {
          completer.Reply(
              fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithSuccess(
                  *result.value().value()));
        } else {
          completer.Reply(
              fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithApplicationError(
                  result.value().error_value()));
        }
      } else {
        completer.Reply(
            fidl_clientsuite::wire::NonEmptyResultWithErrorClassification::WithFidlError(
                clienttest_util::ClassifyError(result)));
      }
    });
  }

  void ReceiveClosedEvents(ReceiveClosedEventsRequestView request,
                           ReceiveClosedEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::WireAsyncEventHandler<fidl_clientsuite::ClosedTarget> {
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

      void SetClient(fidl::WireSharedClient<fidl_clientsuite::ClosedTarget> client) {
        client_ = std::move(client);
      }

     private:
      async_dispatcher_t* dispatcher_;
      fidl::WireSharedClient<fidl_clientsuite::ClosedTarget> client_;
      fidl::SyncClient<fidl_clientsuite::ClosedTargetEventReporter> reporter_;
    };

    auto handler = std::make_unique<EventHandler>(dispatcher_, std::move(request->reporter));
    auto* handler_ptr = handler.get();
    auto client =
        fidl::WireSharedClient(std::move(request->target), dispatcher_, std::move(handler));
    handler_ptr->SetClient(std::move(client));
    completer.Reply();
  }

  void ReceiveAjarEvents(ReceiveAjarEventsRequestView request,
                         ReceiveAjarEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::WireAsyncEventHandler<fidl_clientsuite::AjarTarget> {
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

      void SetClient(fidl::WireSharedClient<fidl_clientsuite::AjarTarget> client) {
        client_ = std::move(client);
      }

     private:
      async_dispatcher_t* dispatcher_;
      fidl::WireSharedClient<fidl_clientsuite::AjarTarget> client_;
      fidl::SyncClient<fidl_clientsuite::AjarTargetEventReporter> reporter_;
    };

    auto handler = std::make_unique<EventHandler>(dispatcher_, std::move(request->reporter));
    auto* handler_ptr = handler.get();
    auto client =
        fidl::WireSharedClient(std::move(request->target), dispatcher_, std::move(handler));
    handler_ptr->SetClient(std::move(client));
    completer.Reply();
  }

  void ReceiveOpenEvents(ReceiveOpenEventsRequestView request,
                         ReceiveOpenEventsCompleter::Sync& completer) override {
    class EventHandler : public fidl::WireAsyncEventHandler<fidl_clientsuite::OpenTarget> {
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

      void StrictEvent(fidl::WireEvent<fidl_clientsuite::OpenTarget::StrictEvent>* event) override {
        ReportEvent(fidl_clientsuite::OpenTargetEventReport::WithStrictEvent({}));
      }

      void FlexibleEvent(
          fidl::WireEvent<fidl_clientsuite::OpenTarget::FlexibleEvent>* event) override {
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

      void SetClient(fidl::WireSharedClient<fidl_clientsuite::OpenTarget> client) {
        client_ = std::move(client);
      }

     private:
      async_dispatcher_t* dispatcher_;
      fidl::WireSharedClient<fidl_clientsuite::OpenTarget> client_;
      fidl::SyncClient<fidl_clientsuite::OpenTargetEventReporter> reporter_;
    };

    auto handler = std::make_unique<EventHandler>(dispatcher_, std::move(request->reporter));
    auto* handler_ptr = handler.get();
    auto client =
        fidl::WireSharedClient(std::move(request->target), dispatcher_, std::move(handler));
    handler_ptr->SetClient(std::move(client));
    completer.Reply();
  }

 private:
  async_dispatcher_t* dispatcher_;
};

int main(int argc, const char** argv) {
  std::cout << "CPP wire sync client: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  ZX_ASSERT(outgoing.ServeFromStartupInfo().is_ok());
  RunnerServer runner_server(loop.dispatcher());
  auto result = outgoing.AddProtocol<fidl_clientsuite::Runner>(&runner_server);
  ZX_ASSERT(result.is_ok());

  std::cout << "CPP wire sync client: ready!" << std::endl;
  return loop.Run();
}
