// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_suite.h"

#include <fuchsia/test/cpp/fidl.h>
#include <zircon/errors.h>

#include "lib/fidl/cpp/clone.h"
#include "src/lib/fxl/logging.h"

namespace example {

using fuchsia::test::Case;
using fuchsia::test::Result;

TestSuite::TestSuite(async::Loop* loop, std::vector<TestInput> inputs, Options options)
    : binding_(this), test_inputs_(std::move(inputs)), options_(options), loop_(loop) {}

void TestSuite::GetTests(::fidl::InterfaceRequest<fuchsia::test::CaseIterator> request) {
  if (options_.close_channel_get_tests) {
    binding_.Close(ZX_ERR_PEER_CLOSED);
    return;
  }
  if (options_.dont_service_get_tests) {
    return;
  }

  std::vector<Case> cases;

  for (auto& test_input : test_inputs_) {
    Case test_case;
    test_case.set_name(test_input.name);
    cases.push_back(std::move(test_case));
  }

  auto service = std::make_unique<CaseIterator>(
      test_inputs_, [this](CaseIterator* ptr) { case_iterator_bindings_.RemoveBinding(ptr); });
  case_iterator_bindings_.AddBinding(std::move(service), std::move(request));
}

void TestSuite::Run(std::vector<fuchsia::test::Invocation> tests,
                    fuchsia::test::RunOptions /*unused*/,
                    fidl::InterfaceHandle<fuchsia::test::RunListener> run_listener) {
  if (options_.close_channel_run) {
    binding_.Close(ZX_ERR_PEER_CLOSED);
    return;
  }
  if (options_.dont_service_run) {
    return;
  }
  fuchsia::test::RunListenerPtr ptr;
  ptr.Bind(std::move(run_listener));
  for (auto& test_invocation : tests) {
    const auto& test_name = test_invocation.name();
    zx::socket log_sock;
    zx::socket test_case_log;
    zx::socket::create(0, &log_sock, &test_case_log);
    fuchsia::test::CaseListenerPtr case_list_ptr;
    ptr->OnTestCaseStarted(fidl::Clone(test_invocation), std::move(log_sock),
                           case_list_ptr.NewRequest());
    std::string msg1 = "log1 for " + test_name + "\n";
    std::string msg2 = "log2 for " + test_name + "\n";
    std::string msg3 = "log3 for " + test_name + "\n";
    zx_status_t status;
    FXL_CHECK(ZX_OK == (status = test_case_log.write(0, msg1.data(), msg1.length(), nullptr)))
        << status;
    FXL_CHECK(ZX_OK == (status = test_case_log.write(0, msg2.data(), msg2.length(), nullptr)))
        << status;
    FXL_CHECK(ZX_OK == (status = test_case_log.write(0, msg3.data(), msg3.length(), nullptr)))
        << status;
    Result result;

    bool send_finished_event = true;
    for (auto& test_input : test_inputs_) {
      if (test_input.name == test_name) {
        if (test_input.set_result_status) {
          result.set_status(test_input.status);
        }
        send_finished_event = !test_input.incomplete_test;
        break;
      }
    }
    if (send_finished_event) {
      case_list_ptr->Finished(std::move(result));
    }
  }
  if (!options_.dont_send_on_finish_event) {
    ptr->OnFinished();
  }
}

fidl::InterfaceRequestHandler<fuchsia::test::Suite> TestSuite::GetHandler() {
  auto handler = [this](fidl::InterfaceRequest<fuchsia::test::Suite> request) {
    binding_.Bind(std::move(request));
  };

  binding_.set_error_handler([this](zx_status_t /*unused*/) { loop_->Shutdown(); });
  return handler;
}

void CaseIterator::GetNext(GetNextCallback callback) {
  std::vector<Case> cases;
  // Send the next page of Cases
  const int MAX_CASES_PER_PAGE = 50;
  int sent = 0;
  for (; iter_ < test_inputs_.end() && sent < MAX_CASES_PER_PAGE; iter_++, sent++) {
    Case test_case;
    test_case.set_name(iter_->name);
    cases.emplace_back(std::move(test_case));
  }
  callback(std::move(cases));

  if (sent == 0) {
    done_callback_(this);
  }
};

}  // namespace example
