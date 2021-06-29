// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_suite.h"

#include <fuchsia/test/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <measure_tape/hlcpp/measure_tape_for_case.h>

#include "lib/fidl/cpp/clone.h"

namespace example {

using fuchsia::test::Case;
using fuchsia::test::Result;

TestSuite::TestSuite(async::Loop* loop, std::vector<TestInput> inputs, Options options)
    : binding_(this), test_inputs_(std::move(inputs)), options_(options), loop_(loop) {
  for (auto& test_input : test_inputs_) {
    if (test_input.disabled) {
      disabled_tests_.insert(test_input.name);
    }
  }
}

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
    test_case.set_enabled(!test_input.disabled);
    cases.push_back(std::move(test_case));
  }

  auto service = std::make_unique<CaseIterator>(
      test_inputs_, [this](CaseIterator* ptr) { case_iterator_bindings_.RemoveBinding(ptr); });
  case_iterator_bindings_.AddBinding(std::move(service), std::move(request));
}

void TestSuite::Run(std::vector<fuchsia::test::Invocation> tests,
                    fuchsia::test::RunOptions run_options,
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
    zx::socket stdout_sock;
    zx::socket case_stdout;
    zx::socket::create(0, &stdout_sock, &case_stdout);
    fuchsia::test::CaseListenerPtr case_list_ptr;
    fuchsia::test::StdHandles std_handles;
    std_handles.set_out(std::move(stdout_sock));
    ptr->OnTestCaseStarted(fidl::Clone(test_invocation), std::move(std_handles),
                           case_list_ptr.NewRequest());
    const bool should_skip_test = ShouldSkipTest(run_options, test_name);
    if (!should_skip_test) {
      std::string msg1 = "log1 for " + test_name + "\n";
      std::string msg2 = "log2 for " + test_name + "\n";
      std::string msg3 = "log3 for " + test_name + "\n";
      zx_status_t status;
      FX_CHECK(ZX_OK == (status = case_stdout.write(0, msg1.data(), msg1.length(), nullptr)))
          << status;
      FX_CHECK(ZX_OK == (status = case_stdout.write(0, msg2.data(), msg2.length(), nullptr)))
          << status;
      FX_CHECK(ZX_OK == (status = case_stdout.write(0, msg3.data(), msg3.length(), nullptr)))
          << status;
    }
    Result result;

    bool send_finished_event = true;
    for (auto& test_input : test_inputs_) {
      if (test_input.name == test_name) {
        if (test_input.set_result_status) {
          result.set_status(should_skip_test ? fuchsia::test::Status::SKIPPED : test_input.status);
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

bool TestSuite::ShouldSkipTest(const fuchsia::test::RunOptions& run_options,
                               const std::string& test_name) const {
  // Disabled tests are excluded && this test is disabled.
  return !run_options.include_disabled_tests() &&
         (disabled_tests_.find(test_name) != disabled_tests_.end());
}

fidl::InterfaceRequestHandler<fuchsia::test::Suite> TestSuite::GetHandler() {
  auto handler = [this](fidl::InterfaceRequest<fuchsia::test::Suite> request) {
    binding_.Bind(std::move(request));
  };

  binding_.set_error_handler([this](zx_status_t /*unused*/) { loop_->Shutdown(); });
  return handler;
}

void CaseIterator::GetNext(GetNextCallback callback) {
  const size_t page_overhead = sizeof(fidl_message_header_t) + sizeof(fidl_vector_t);
  const size_t max_bytes = ZX_CHANNEL_MAX_MSG_BYTES - page_overhead;
  size_t bytes_used = 0;

  std::vector<Case> cases;
  // Send the next page of Cases
  for (; iter_ < test_inputs_.end(); iter_++) {
    Case test_case;
    test_case.set_name(iter_->name);
    bytes_used += measure_tape::fuchsia::test::Measure(test_case).num_bytes;
    if (bytes_used > max_bytes) {
      break;
    }
    cases.emplace_back(std::move(test_case));
  }
  callback(std::move(cases));

  if (bytes_used == 0) {
    done_callback_(this);
  }
};

}  // namespace example
