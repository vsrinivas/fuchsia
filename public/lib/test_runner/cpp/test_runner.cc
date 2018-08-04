// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a TCP service and a fidl service. The TCP portion of this process
// accepts test commands, runs them, waits for completion or error, and reports
// back to the TCP client.
// The TCP protocol is as follows:
// - Client connects, sends a single line representing the test command to run:
//   run <test_id> <shell command to run>\n
// - To send a log message, we send to the TCP client:
//   <test_id> log <msg>
// - Once the test is done, we reply to the TCP client:
//   <test_id> teardown pass|fail\n
//
// The <test_id> is an unique ID string that the TCP client gives us per test;
// we tag our replies and device logs with it so the TCP client can identify
// device logs (and possibly if multiple tests are run at the same time).
//
// The shell command representing the running test is launched in a new
// Environment for easy teardown. This Environment
// contains a TestRunner service (see test_runner.fidl). The applications
// launched by the shell command (which may launch more than 1 process) may use
// the |TestRunner| service to signal completion of the test, and also provides
// a way to signal process crashes.

// TODO(vardhan): Make it possible to run multiple tests within the same test
// runner environment, without teardown; useful for testing modules, which may
// not need to tear down device_runner.

#include "lib/test_runner/cpp/test_runner.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <vector>

#include <lib/async/default.h>

#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/fxl/type_converter.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace test_runner {

TestRunObserver::~TestRunObserver() = default;

TestRunnerImpl::TestRunnerImpl(fidl::InterfaceRequest<TestRunner> request,
                               TestRunContext* test_run_context)
    : binding_(this, std::move(request)), test_run_context_(test_run_context) {
  binding_.set_error_handler([this] {
    if (termination_task_.is_pending()) {
      FXL_LOG(INFO) << "Test " << program_name_ << " terminated as expected.";
      // Client terminated but that was expected.
      termination_task_.Cancel();
      if (teardown_after_termination_) {
        Teardown([] {});
      } else {
        test_run_context_->StopTrackingClient(this, false);
      }
    } else {
      test_run_context_->StopTrackingClient(this, true);
    }
  });
}

const std::string& TestRunnerImpl::program_name() const {
  return program_name_;
}

bool TestRunnerImpl::waiting_for_termination() const {
  return termination_task_.is_pending();
}

void TestRunnerImpl::TeardownAfterTermination() {
  teardown_after_termination_ = true;
}

void TestRunnerImpl::Identify(fidl::StringPtr program_name,
                              IdentifyCallback callback) {
  program_name_ = program_name;
  callback();
}

void TestRunnerImpl::ReportResult(TestResult result) {
  test_run_context_->ReportResult(std::move(result));
}

void TestRunnerImpl::Fail(fidl::StringPtr log_message) {
  test_run_context_->Fail(log_message);
}

void TestRunnerImpl::Done(DoneCallback callback) {
  // Acknowledge that we got the Done() call.
  callback();
  test_run_context_->StopTrackingClient(this, false);
}

void TestRunnerImpl::Teardown(TeardownCallback callback) {
  // Acknowledge that we got the Teardown() call.
  callback();
  test_run_context_->Teardown(this);
}

void TestRunnerImpl::WillTerminate(const double withinSeconds) {
  if (termination_task_.is_pending()) {
    Fail(program_name_ + " called WillTerminate more than once.");
    return;
  }
  termination_task_.set_handler([this, withinSeconds](async_dispatcher_t*,
                                                      async::Task*,
                                                      zx_status_t status) {
    FXL_LOG(ERROR) << program_name_ << " termination timed out after "
                   << withinSeconds << "s.";
    binding_.set_error_handler(nullptr);
    Fail("Termination timed out.");
    if (teardown_after_termination_) {
      Teardown([] {});
    }
    test_run_context_->StopTrackingClient(this, false);
  });
  termination_task_.PostDelayed(async_get_default_dispatcher(),
                                zx::sec(withinSeconds));
}

void TestRunnerImpl::SetTestPointCount(int64_t count) {
  // Check that the count hasn't been set yet.
  FXL_CHECK(remaining_test_points_ == -1);

  // Check that the count makes sense.
  FXL_CHECK(count >= 0);

  remaining_test_points_ = count;
}

void TestRunnerImpl::PassTestPoint() {
  // Check that the test point count has been set.
  FXL_CHECK(remaining_test_points_ >= 0);

  if (remaining_test_points_ == 0) {
    Fail(program_name_ + " passed more test points than expected.");
    return;
  }

  remaining_test_points_--;
}

TestRunContext::TestRunContext(
    std::shared_ptr<component::StartupContext> app_context,
    TestRunObserver* connection, const std::string& test_id,
    const std::string& url, const std::vector<std::string>& args)
    : test_runner_connection_(connection), test_id_(test_id), success_(true) {
  // 1. Make a child environment to run the command.
  child_env_scope_ =
      std::make_unique<Scope>(app_context->environment(), "test_runner_env");

  // 1.1 Setup child environment services
  child_env_scope_->AddService<TestRunner>(
      [this](fidl::InterfaceRequest<TestRunner> request) {
        test_runner_clients_.push_back(
            std::make_unique<TestRunnerImpl>(std::move(request), this));
      });
  child_env_scope_->AddService<TestRunnerStore>(
      [this](fidl::InterfaceRequest<TestRunnerStore> request) {
        test_runner_store_.AddBinding(std::move(request));
      });

  // 2. Launch the test command.
  fuchsia::sys::LauncherPtr launcher;
  child_env_scope_->environment()->GetLauncher(launcher.NewRequest());

  fuchsia::sys::LaunchInfo info;
  info.url = url;
  info.arguments = fxl::To<fidl::VectorPtr<fidl::StringPtr>>(args);
  launcher->CreateComponent(std::move(info), child_controller_.NewRequest());

  // If the child app closes, the test is reported as a failure.
  child_controller_.set_error_handler([this] {
    FXL_LOG(WARNING) << "Child app connection closed unexpectedly. Remaining "
                        "TestRunner clients = "
                     << test_runner_clients_.size();
    test_runner_connection_->Teardown(test_id_, false);
  });
}

void TestRunContext::ReportResult(TestResult result) {
  if (result.failed) {
    success_ = false;
  }

  rapidjson::Document doc;
  rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
  doc.SetObject();
  doc.AddMember("name", std::string(result.name), allocator);
  doc.AddMember("elapsed", result.elapsed, allocator);
  doc.AddMember("failed", result.failed, allocator);
  doc.AddMember("message", std::string(result.message), allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  test_runner_connection_->SendMessage(test_id_, "result", buffer.GetString());
}

void TestRunContext::Fail(const fidl::StringPtr& log_msg) {
  success_ = false;
  std::string msg("FAIL: ");
  msg += log_msg;
  test_runner_connection_->SendMessage(test_id_, "log", msg);
}

void TestRunContext::StopTrackingClient(TestRunnerImpl* client, bool crashed) {
  if (crashed) {
    FXL_LOG(WARNING) << client->program_name()
                     << " finished without calling"
                        " test_runner::reporting::Done().";
    test_runner_connection_->Teardown(test_id_, false);
    return;
  }

  if (client->TestPointsRemaining()) {
    FXL_LOG(WARNING) << client->program_name()
                     << " finished without passing all test points.";
    test_runner_connection_->Teardown(test_id_, false);
    return;
  }

  auto find_it =
      std::find_if(test_runner_clients_.begin(), test_runner_clients_.end(),
                   [client](const std::unique_ptr<TestRunnerImpl>& client_it) {
                     return client_it.get() == client;
                   });

  FXL_DCHECK(find_it != test_runner_clients_.end());
  test_runner_clients_.erase(find_it);
}

void TestRunContext::Teardown(TestRunnerImpl* teardown_client) {
  bool waiting_for_termination = false;
  for (auto& client : test_runner_clients_) {
    if (teardown_client == client.get()) {
      continue;
    }
    if (client->waiting_for_termination()) {
      client->TeardownAfterTermination();
      FXL_LOG(INFO) << "Teardown blocked by test waiting for termination: "
                    << client->program_name();
      waiting_for_termination = true;
      continue;
    }
    FXL_LOG(ERROR) << "Test " << client->program_name()
                   << " not done before Teardown().";
    success_ = false;
  }
  if (waiting_for_termination) {
    StopTrackingClient(teardown_client, false);
  } else {
    // Once teardown is signalled, it's no longer a test failure if the child
    // app controller connection closes. If this is not reset, a connection
    // close may call test_runner_connection_->Teardown() again and override the
    // success status set here.
    child_controller_.set_error_handler(nullptr);
    test_runner_connection_->Teardown(test_id_, success_);
  }
}

}  // namespace test_runner
