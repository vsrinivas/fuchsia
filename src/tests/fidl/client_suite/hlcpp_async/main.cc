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
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <iostream>
#include <memory>

#include "lib/fidl/cpp/unknown_interactions_hlcpp.h"
#include "lib/fit/function.h"
#include "src/tests/fidl/client_suite/hlcpp_util/error_util.h"

template <typename Client, typename Functype>
class SharedCallbackAndClient {};

template <typename Client, typename Result, typename... Args>
class SharedCallbackAndClient<Client, Result(Args...)> {
 public:
  explicit SharedCallbackAndClient(Client&& client, fit::function<Result(Args...)>&& function) {
    shared_ =
        std::make_shared<Shared>(std::move(client), [function = std::move(function)](Args... args) {
          return function(std::forward<Args>(args)...);
        });
  }
  SharedCallbackAndClient(SharedCallbackAndClient&) = default;
  SharedCallbackAndClient& operator=(SharedCallbackAndClient const&) = default;
  SharedCallbackAndClient(SharedCallbackAndClient&&) noexcept = default;
  SharedCallbackAndClient& operator=(SharedCallbackAndClient&&) noexcept = default;

  Client& client() { return shared_->client; }

  explicit operator bool() const { return shared_->callback != nullptr; }

  Result operator()(Args... args) const {
    ZX_ASSERT_MSG(shared_->callback, "Callback already called");
    // Make sure the client is discarded when the callback is run.
    auto client = std::move(shared_->client);
    return shared_->callback(std::forward<Args>(args)...);
  }

 private:
  struct Shared {
    explicit Shared(Client&& client, fit::callback<Result(Args...)>&& callback)
        : client(std::move(client)), callback(std::move(callback)) {}

    Client client;
    fit::callback<Result(Args...)> callback;
  };
  std::shared_ptr<Shared> shared_;
};

template <typename Client, typename Functype>
SharedCallbackAndClient(Client&& client, fit::function<Functype>&& function)
    -> SharedCallbackAndClient<Client, Functype>;

template <typename Client, typename Reporter>
class SharedClientAndReporter {
 public:
  SharedClientAndReporter(Client&& client, Reporter&& reporter) {
    shared_ = std::make_shared<Shared>(std::move(client), std::move(reporter));
  }

  Client& client() { return shared_->client; }
  const Client& client() const { return shared_->client; }
  Reporter& reporter() { return shared_->reporter; }
  const Reporter& reporter() const { return shared_->reporter; }

  explicit operator bool() const { return reporter().is_bound(); }

  void Close() {
    if (*this) {
      auto take_client = std::move(client());
      auto take_reporter = std::move(reporter());
    }
  }

 private:
  struct Shared {
    Shared(Client&& client, Reporter&& reporter)
        : client(std::move(client)), reporter(std::move(reporter)) {}

    Client client;
    Reporter reporter;
  };
  std::shared_ptr<Shared> shared_;
};

class RunnerServer : public fidl::clientsuite::Runner {
 public:
  RunnerServer() = default;

  void IsTestEnabled(fidl::clientsuite::Test test, IsTestEnabledCallback callback) override {
    switch (test) {
      default:
        callback(true);
        return;
    }
  }

  void CheckAlive(CheckAliveCallback callback) override { callback(); }

  void CallTwoWayNoPayload(fidl::InterfaceHandle<fidl::clientsuite::ClosedTarget> target,
                           CallTwoWayNoPayloadCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->TwoWayNoPayload([client_callback]() {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
    });
  }

  void CallStrictOneWay(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                        CallStrictOneWayCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->StrictOneWay();
    if (client_callback) {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
    }
  }

  void CallFlexibleOneWay(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                          CallFlexibleOneWayCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->FlexibleOneWay();
    if (client_callback) {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
    }
  }

  void CallStrictTwoWay(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                        CallStrictTwoWayCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->StrictTwoWay([client_callback]() {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
    });
  }

  void CallStrictTwoWayFields(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                              CallStrictTwoWayFieldsCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::NonEmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->StrictTwoWayFields([client_callback](auto result) {
      client_callback(fidl::clientsuite::NonEmptyResultClassification::WithSuccess(
          fidl::clientsuite::NonEmptyPayload(result)));
    });
  }

  void CallStrictTwoWayErr(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                           CallStrictTwoWayErrCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->StrictTwoWayErr([client_callback](auto result) {
      if (result.is_response()) {
        client_callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithSuccess({}));
      } else if (result.is_err()) {
        client_callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithApplicationError(
            std::move(result.err())));
      } else {
        ZX_PANIC("invalid tag");
      }
    });
  }

  void CallStrictTwoWayFieldsErr(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                                 CallStrictTwoWayFieldsErrCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::NonEmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->StrictTwoWayFieldsErr([client_callback](auto result) {
      if (result.is_response()) {
        client_callback(fidl::clientsuite::NonEmptyResultWithErrorClassification::WithSuccess(
            std::move(result.response())));
      } else if (result.is_err()) {
        client_callback(
            fidl::clientsuite::NonEmptyResultWithErrorClassification::WithApplicationError(
                std::move(result.err())));
      } else {
        ZX_PANIC("invalid tag");
      }
    });
  }

  void CallFlexibleTwoWay(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                          CallFlexibleTwoWayCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->FlexibleTwoWay([client_callback](auto result) {
      if (result.is_response()) {
        client_callback(fidl::clientsuite::EmptyResultClassification::WithSuccess({}));
      } else if (result.is_transport_err()) {
        ZX_ASSERT(result.transport_err() == fidl::TransportErr::kUnknownMethod);
        client_callback(fidl::clientsuite::EmptyResultClassification::WithFidlError(
            fidl::clientsuite::FidlErrorKind::UNKNOWN_METHOD));
      } else {
        ZX_PANIC("invalid tag");
      }
    });
  }

  void CallFlexibleTwoWayFields(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                                CallFlexibleTwoWayFieldsCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::NonEmptyResultClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->FlexibleTwoWayFields([client_callback](auto result) {
      if (result.is_response()) {
        client_callback(fidl::clientsuite::NonEmptyResultClassification::WithSuccess(
            std::move(result.response())));
      } else if (result.is_transport_err()) {
        ZX_ASSERT(result.transport_err() == fidl::TransportErr::kUnknownMethod);
        client_callback(fidl::clientsuite::NonEmptyResultClassification::WithFidlError(
            fidl::clientsuite::FidlErrorKind::UNKNOWN_METHOD));
      } else {
        ZX_PANIC("invalid tag");
      }
    });
  }

  void CallFlexibleTwoWayErr(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                             CallFlexibleTwoWayErrCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->FlexibleTwoWayErr([client_callback](auto result) {
      if (result.is_response()) {
        client_callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithSuccess({}));
      } else if (result.is_err()) {
        client_callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithApplicationError(
            std::move(result.err())));
      } else if (result.is_transport_err()) {
        ZX_ASSERT(result.transport_err() == fidl::TransportErr::kUnknownMethod);
        client_callback(fidl::clientsuite::EmptyResultWithErrorClassification::WithFidlError(
            fidl::clientsuite::FidlErrorKind::UNKNOWN_METHOD));
      } else {
        ZX_PANIC("invalid tag");
      }
    });
  }

  void CallFlexibleTwoWayFieldsErr(::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
                                   CallFlexibleTwoWayFieldsErrCallback callback) override {
    SharedCallbackAndClient client_callback(target.Bind(), std::move(callback));
    client_callback.client().set_error_handler([client_callback](auto status) {
      client_callback(fidl::clientsuite::NonEmptyResultWithErrorClassification::WithFidlError(
          clienttest_util::ClassifyError(status)));
    });
    client_callback.client()->FlexibleTwoWayFieldsErr([client_callback](auto result) {
      if (result.is_response()) {
        client_callback(fidl::clientsuite::NonEmptyResultWithErrorClassification::WithSuccess(
            std::move(result.response())));
      } else if (result.is_err()) {
        client_callback(
            fidl::clientsuite::NonEmptyResultWithErrorClassification::WithApplicationError(
                std::move(result.err())));
      } else if (result.is_transport_err()) {
        ZX_ASSERT(result.transport_err() == fidl::TransportErr::kUnknownMethod);
        client_callback(fidl::clientsuite::NonEmptyResultWithErrorClassification::WithFidlError(
            fidl::clientsuite::FidlErrorKind::UNKNOWN_METHOD));
      } else {
        ZX_PANIC("invalid tag");
      }
    });
  }

  void ReceiveClosedEvents(
      ::fidl::InterfaceHandle<::fidl::clientsuite::ClosedTarget> target,
      ::fidl::InterfaceHandle<::fidl::clientsuite::ClosedTargetEventReporter> reporter,
      ReceiveClosedEventsCallback callback) override {
    SharedClientAndReporter client_reporter(target.Bind(), reporter.Bind());
    client_reporter.reporter().set_error_handler([client_reporter](auto status) mutable {
      client_reporter.Close();
      ZX_ASSERT_MSG(status == ZX_OK || status == ZX_ERR_PEER_CLOSED,
                    "Unexpected status from reporter: %d", status);
    });
    client_reporter.client().set_error_handler([client_reporter](auto status) {
      if (client_reporter) {
        if (status != ZX_OK) {
          client_reporter.reporter()->ReportEvent(
              fidl::clientsuite::ClosedTargetEventReport::WithFidlError(
                  clienttest_util::ClassifyError(status)));
        }
      }
    });
    callback();
  }

  void ReceiveAjarEvents(
      ::fidl::InterfaceHandle<::fidl::clientsuite::AjarTarget> target,
      ::fidl::InterfaceHandle<::fidl::clientsuite::AjarTargetEventReporter> reporter,
      ReceiveAjarEventsCallback callback) override {
    SharedClientAndReporter client_reporter(target.Bind(), reporter.Bind());
    client_reporter.reporter().set_error_handler([client_reporter](auto status) mutable {
      client_reporter.Close();
      ZX_ASSERT_MSG(status == ZX_OK || status == ZX_ERR_PEER_CLOSED,
                    "Unexpected status from reporter: %d", status);
    });
    client_reporter.client().set_error_handler([client_reporter](auto status) {
      if (client_reporter) {
        if (status != ZX_OK) {
          client_reporter.reporter()->ReportEvent(
              fidl::clientsuite::AjarTargetEventReport::WithFidlError(
                  clienttest_util::ClassifyError(status)));
        }
      }
    });
    client_reporter.client().events().handle_unknown_event = [client_reporter](auto ordinal) {
      if (client_reporter) {
        client_reporter.reporter()->ReportEvent(
            fidl::clientsuite::AjarTargetEventReport::WithUnknownEvent({.ordinal = ordinal}));
      }
    };
    callback();
  }

  void ReceiveOpenEvents(
      ::fidl::InterfaceHandle<::fidl::clientsuite::OpenTarget> target,
      ::fidl::InterfaceHandle<::fidl::clientsuite::OpenTargetEventReporter> reporter,
      ReceiveOpenEventsCallback callback) override {
    SharedClientAndReporter client_reporter(target.Bind(), reporter.Bind());
    client_reporter.reporter().set_error_handler([client_reporter](auto status) mutable {
      client_reporter.Close();
      ZX_ASSERT_MSG(status == ZX_OK || status == ZX_ERR_PEER_CLOSED,
                    "Unexpected status from reporter: %d", status);
    });
    client_reporter.client().set_error_handler([client_reporter](auto status) {
      if (client_reporter) {
        if (status != ZX_OK) {
          client_reporter.reporter()->ReportEvent(
              fidl::clientsuite::OpenTargetEventReport::WithFidlError(
                  clienttest_util::ClassifyError(status)));
        }
      }
    });
    client_reporter.client().events().handle_unknown_event = [client_reporter](auto ordinal) {
      if (client_reporter) {
        client_reporter.reporter()->ReportEvent(
            fidl::clientsuite::OpenTargetEventReport::WithUnknownEvent({.ordinal = ordinal}));
      }
    };
    client_reporter.client().events().StrictEvent = [client_reporter]() {
      if (client_reporter) {
        client_reporter.reporter()->ReportEvent(
            fidl::clientsuite::OpenTargetEventReport::WithStrictEvent({}));
      }
    };
    client_reporter.client().events().FlexibleEvent = [client_reporter]() {
      if (client_reporter) {
        client_reporter.reporter()->ReportEvent(
            fidl::clientsuite::OpenTargetEventReport::WithFlexibleEvent({}));
      }
    };
    callback();
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
