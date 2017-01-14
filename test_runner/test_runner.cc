// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a TCP service and a fidl service. The TCP portion of this process
// accepts test commands, runs them, waits for completion or error, and reports
// back to the TCP client.
// The TCP protocol is as follows:
// - Client connects, sends a single line representing the test command to run:
//   run <test_id> <shell command to run>\n
// - Once the test is done, we reply to the TCP client:
//   <test_id> pass|fail\n
//
// The <test_id> is an unique ID string that the TCP client gives us per test;
// we tag our replies and device logs with it so the TCP client can identify
// device logs (and possibly if multiple tests are run at the same time).
//
// The shell command representing the running test is launched in a new
// ApplicationEnvironment for easy teardown. This ApplicationEnvironment
// contains a TestRunner service (see test_runner.fidl). The applications
// launched by the shell command (which may launch more than 1 process) may use
// the |TestRunner| service to signal completion of the test, and also provides
// a way to signal process crashes.

// TODO(vardhan): Make it possible to run more than one test per TCP connection.
// And possibly at the same time. And more than one TCP connection at the same
// time.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <vector>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/fidl/scope.h"
#include "apps/modular/services/test_runner/test_runner.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/tasks/message_loop.h"

// TODO(vardhan): Make listen port command-line configurable.
constexpr uint16_t kListenPort = 8342;        // TCP port
constexpr uint16_t kMaxCommandLength = 2048;  // in bytes.

namespace modular {
namespace {

// This is an ApplicationEnvironment under which our test runs. We expose a
// TestRunner service which the test can use to assert when it is complete.
class TestRunnerScope : public modular::Scope {
 public:
  TestRunnerScope(
      ApplicationEnvironmentPtr parent_env,
      ServiceProviderPtr default_services,
      const std::string& label,
      ServiceProviderImpl::InterfaceRequestHandler<TestRunner> request_handler)
      : Scope(std::move(parent_env), label) {
    service_provider_.SetDefaultServiceProvider(std::move(default_services));
    service_provider_.AddService(request_handler);
  }

 private:
  // |ApplicationEnvironmentHost|:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<ServiceProvider> environment_services) override {
    service_provider_.AddBinding(std::move(environment_services));
  }

  ServiceProviderImpl service_provider_;
};

// Represents a client connection, and is self-owned (it will exit the
// MessageLoop upon completion). TestRunnerConnection receives commands to run
// tests, kicks them off in their own ApplicationEnvironment, provides the
// environment a TestRunner service to report completion, and reports back test
// results.
class TestRunnerConnection : public TestRunner {
 public:
  explicit TestRunnerConnection(int socket_fd,
                                std::shared_ptr<ApplicationContext> app_context)
      : app_context_(app_context),
        socket_(socket_fd),
        test_runner_binding_(this) {
    test_runner_binding_.set_connection_error_handler(
        std::bind(&TestRunnerConnection::Finish, this, false));
  }

  void Start() { ReadAndRunCommand(); }

 private:
  ~TestRunnerConnection() {
    close(socket_);
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  // |TestRunner|:
  void Finish(bool success) {
    // IMPORTANT: leave this log here, exactly as it is. Currently, tests
    // launched from host (e.g. Linux) grep for this text to figure out the
    // amount of the log to associate with the test.
    FTL_LOG(INFO) << "test_runner: done " << test_id_ << " success=" << success;

    std::stringstream epilogue;
    epilogue << test_id_ << " ";
    epilogue << (success ? "pass" : "fail");

    std::string bytes = epilogue.str();
    write(socket_, bytes.data(), bytes.size());

    delete this;
  }

  // Read an entire line representing the command to run. Blocks until we have a
  // line. Fails if we hit |kMaxCommandLength| chars.
  void ReadAndRunCommand() {
    char buf[kMaxCommandLength];
    size_t read_so_far = 0;
    while (read_so_far < kMaxCommandLength) {
      ssize_t n = read(socket_, buf + read_so_far, sizeof(buf) - read_so_far);
      FTL_CHECK(n > 0);
      read_so_far += n;
      // Is there a line?
      // TODO(vardhan): Will be bad if we receive anything after the new line.
      if (static_cast<char*>(memchr(buf, '\n', read_so_far)) != nullptr) {
        break;
      }
    }
    if (read_so_far < kMaxCommandLength) {
      ftl::StringView command_line(buf, read_so_far);

      // command_parse[0] = "run"
      // command_parse[1] = test_id
      // command_parse[2] = url
      // command_parse[3..] = args (optional)
      std::vector<std::string> command_parse = ftl::SplitStringCopy(
          command_line, " ", ftl::kTrimWhitespace, ftl::kSplitWantNonEmpty);

      FTL_CHECK(command_parse.size() >= 3)
          << "Not enough args. Must be: `run <test id> <command to run>`";
      FTL_CHECK(command_parse[0] == "run")
          << "Only supported command is `run`.";

      test_id_ = command_parse[1];
      FTL_LOG(INFO) << "test_runner: run " << test_id_;

      std::vector<std::string> args;
      for (size_t i = 3; i < command_parse.size(); i++) {
        args.push_back(std::move(command_parse[i]));
      }
      RunCommand(command_parse[2], args);
    } else {
      delete this;
    }
  }

  // If the child application stops without reporting anything, we declare the
  // test a failure.
  void RunCommand(const std::string& url,
                  const std::vector<std::string>& args) {
    // 1. Make a child environment to run the command.
    ApplicationEnvironmentPtr parent_env;
    app_context_->environment()->Duplicate(parent_env.NewRequest());

    ServiceProviderPtr parent_env_services;
    parent_env->GetServices(parent_env_services.NewRequest());

    child_env_scope_ = std::make_unique<TestRunnerScope>(
        std::move(parent_env), std::move(parent_env_services),
        "test_runner_env", [this](fidl::InterfaceRequest<TestRunner> request) {
          test_runner_binding_.Bind(std::move(request));
        });

    // 2. Launch the test command.
    ApplicationLauncherPtr launcher;
    child_env_scope_->environment()->GetApplicationLauncher(
        launcher.NewRequest());

    ApplicationLaunchInfoPtr info = ApplicationLaunchInfo::New();
    info->url = url;
    info->arguments = fidl::Array<fidl::String>::From(args);
    launcher->CreateApplication(std::move(info),
                                child_app_controller_.NewRequest());
    child_app_controller_.set_connection_error_handler(
        std::bind(&TestRunnerConnection::Finish, this, false));
  }

  std::shared_ptr<ApplicationContext> app_context_;

  // Posix fd for the TCP connection.
  int socket_;

  ApplicationControllerPtr child_app_controller_;
  std::unique_ptr<TestRunnerScope> child_env_scope_;

  fidl::Binding<TestRunner> test_runner_binding_;
  // This is a tag that we use to identify the test that was run. For now, it
  // helps distinguish between multiple test outputs to the device log.
  std::string test_id_;
};

// TestRunnerTCPServer is a TCP server that accepts connections and launches
// them as TestRunnerConnection.
class TestRunnerTCPServer {
 public:
  TestRunnerTCPServer(uint16_t port)
      : app_context_(ApplicationContext::CreateFromStartupInfo()) {
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 1. Make a TCP socket.
    listener_ = socket(addr.sin_family, SOCK_STREAM, IPPROTO_TCP);
    FTL_CHECK(listener_ != -1);

    // 2. Bind it to an address.
    FTL_CHECK(bind(listener_, reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)) != -1);

    // 3. Make it a listening socket.
    FTL_CHECK(listen(listener_, 100) != -1);
  }

  ~TestRunnerTCPServer() { close(listener_); }

  // Blocks until there is a new connection.
  TestRunnerConnection* AcceptConnection() {
    int sockfd = accept(listener_, nullptr, nullptr);
    if (sockfd == -1) {
      FTL_LOG(INFO) << "accept() oops";
    }
    return new TestRunnerConnection(sockfd, app_context_);
  }

 private:
  int listener_;
  std::shared_ptr<ApplicationContext> app_context_;
};

}  // namespace
}  // namespace modular

int main() {
  mtl::MessageLoop loop;
  modular::TestRunnerTCPServer server(kListenPort);
  while (1) {
    // TODO(vardhan): Because our sockets are POSIX fds, they don't work with
    // our message loop, so we do some synchronous operations and have to do
    // manipulate the message loop to pass control back and forth. Consider
    // using separate threads for handle message loop vs. fd polling.
    auto* runner = server.AcceptConnection();
    loop.task_runner()->PostTask(
        std::bind(&modular::TestRunnerConnection::Start, runner));
    loop.Run();
  }
  return 0;
}
