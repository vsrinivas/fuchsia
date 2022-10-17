// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.exception/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>
#include <threads.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>

#include <list>
#include <memory>

#include <crashsvc/crashsvc.h>
#include <crashsvc/exception_handler.h>
#include <mini-process/mini-process.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"

namespace {

// Crashsvc will attempt to connect to a |fuchsia.exception.Handler| when it catches an exception.
// We use this fake in order to verify that behaviour.
class StubExceptionHandler final : public fidl::WireServer<fuchsia_exception::Handler> {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher,
                      fidl::ServerEnd<fuchsia_exception::Handler> request) {
    binding_ = fidl::BindServer(dispatcher, std::move(request), this);
    return ZX_OK;
  }

  // fuchsia.exception.Handler
  void OnException(OnExceptionRequestView request, OnExceptionCompleter::Sync& completer) override {
    exception_count_++;
    if (respond_sync_) {
      completer.Reply();
    } else {
      on_exception_completers_.push_back(completer.ToAsync());
    }
  }

  void IsActive(IsActiveCompleter::Sync& completer) override {
    if (is_active_) {
      completer.Reply();
    } else {
      is_active_completers_.push_back(completer.ToAsync());
    }
  }

  void SendAsyncResponses() {
    for (auto& completer : on_exception_completers_) {
      completer.Reply();
    }

    on_exception_completers_.clear();
  }

  void SetRespondSync(bool val) { respond_sync_ = val; }

  void SetIsActive(bool val) {
    is_active_ = val;
    if (!is_active_) {
      return;
    }

    for (auto& completer : is_active_completers_) {
      completer.Reply();
    }

    is_active_completers_.clear();
  }

  zx_status_t Unbind() {
    if (!binding_.has_value()) {
      return ZX_ERR_BAD_STATE;
    }
    binding_.value().Close(ZX_ERR_PEER_CLOSED);
    binding_ = std::nullopt;
    return ZX_OK;
  }

  bool HasClient() const { return binding_.has_value(); }

  int exception_count() const { return exception_count_; }

 private:
  std::optional<fidl::ServerBindingRef<fuchsia_exception::Handler>> binding_;

  int exception_count_ = 0;
  bool respond_sync_{true};
  bool is_active_{true};
  std::list<OnExceptionCompleter::Async> on_exception_completers_;
  std::list<IsActiveCompleter::Async> is_active_completers_;
};

// Exposes the services through a virtual directory that crashsvc uses in order to connect to
// services. We use this to inject a |StubExceptionHandler| for the |fuchsia.exception.Handler|
// service.
class FakeService {
 public:
  explicit FakeService(async_dispatcher_t* dispatcher) : vfs_(dispatcher) {
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_exception::Handler>,
                       fbl::MakeRefCounted<fs::Service>(
                           [this, dispatcher](fidl::ServerEnd<fuchsia_exception::Handler> request) {
                             return exception_handler_.Connect(dispatcher, std::move(request));
                           }));

    // We serve this directory.
    zx::result remote = fidl::CreateEndpoints(&svc_local_);
    ASSERT_OK(remote.status_value());
    vfs_.ServeDirectory(root_dir, std::move(remote.value()));
  }

  StubExceptionHandler& exception_handler() { return exception_handler_; }
  const zx::channel& service_channel() const { return svc_local_.channel(); }

 private:
  fs::SynchronousVfs vfs_;
  StubExceptionHandler exception_handler_;
  fidl::ClientEnd<fuchsia_io::Directory> svc_local_;
};

class TestBase : public zxtest::Test {
 public:
  explicit TestBase(const async_loop_config_t* config)
      : loop_(config), fake_service_(loop_.dispatcher()) {}

  void SetUp() final {
    ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &parent_job_));
    ASSERT_OK(parent_job_.create_exception_channel(0, &exception_channel_));

    ASSERT_OK(zx::job::create(parent_job_, 0, &job_));
  }

  void TearDown() override { loop().Shutdown(); }

  // Synchronously creates a process under |job_| that immediately crashes.
  void GenerateException() const {
    zx::process process;
    zx::thread thread;
    GenerateException(&process, &thread);
  }

  // Synchronously creates a process under |job_| that requests a backtrace.
  void GenerateBacktraceRequest() const {
    zx::channel command_channel;
    zx::process process;
    zx::thread thread;
    ASSERT_NO_FATAL_FAILURE(CreateMiniProcess(&process, &thread, &command_channel));

    // Use mini_process_cmd() here to send and block until we get a response.
    printf("Intentionally dumping test thread '%s', the following dump is expected\n", kTaskName);
    ASSERT_OK(mini_process_cmd(command_channel.get(), MINIP_CMD_BACKTRACE_REQUEST, nullptr));
  }

  // Generates a new exception so the ExceptionHandler FIDL message sent by crashsvc can be
  // analyzed.
  void AnalyzeCrash() {
    zx::process process;
    zx::thread thread;
    ASSERT_NO_FATAL_FAILURE(GenerateException(&process, &thread));

    // Wait for the exception to bubble up and reset it so it doesn't escape the test environment.
    auto exception = ReadPendingException(zx::duration::infinite());
    ASSERT_OK(exception);
    exception->first.reset();
  }

  // Synchronously read and return the exception from |exception_channel_| within |limit|.
  zx::result<std::pair<zx::exception, zx_exception_info_t>> ReadPendingException(
      const zx::duration limit) const {
    if (const auto status =
            exception_channel().wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(limit), nullptr);
        status != ZX_OK) {
      return zx::error(status);
    }

    zx_exception_info_t info;
    zx::exception exception;
    if (const auto status = exception_channel().read(0, &info, exception.reset_and_get_address(),
                                                     sizeof(info), 1, nullptr, nullptr);
        status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(std::make_pair(std::move(exception), std::move(info)));
  }

  const async::Loop& loop() const { return loop_; }
  async::Loop& loop() { return loop_; }

  const zx::channel& svc_channel() const { return fake_service_.service_channel(); }

  StubExceptionHandler& stub_handler() { return fake_service_.exception_handler(); }

  const zx::job& job() const { return job_; }
  zx::job& job() { return job_; }

  const zx::channel& exception_channel() const { return exception_channel_; }
  zx::channel& exception_channel() { return exception_channel_; }

 private:
  static constexpr char kTaskName[] = "crashsvc-test";
  static constexpr uint32_t kTaskNameLen = sizeof(kTaskName) - 1;

  // Creates a mini-process under |job_|.
  void CreateMiniProcess(zx::process* process, zx::thread* thread,
                         zx::channel* command_channel) const {
    zx::vmar vmar;
    ASSERT_OK(zx::process::create(job(), kTaskName, kTaskNameLen, 0, process, &vmar));
    ASSERT_OK(zx::thread::create(*process, kTaskName, kTaskNameLen, 0, thread));

    zx::event event;
    ASSERT_OK(zx::event::create(0, &event));

    ASSERT_OK(start_mini_process_etc(process->get(), thread->get(), vmar.get(), event.release(),
                                     true, command_channel->reset_and_get_address()));
  }

  // Synchronously creates a process under |job_| that immediately crashes.
  void GenerateException(zx::process* process, zx::thread* thread) const {
    zx::channel command_channel;
    ASSERT_NO_FATAL_FAILURE(CreateMiniProcess(process, thread, &command_channel));

    // Use mini_process_cmd_send() here to send but not wait for a response
    // so we can handle the exception.
    printf("Intentionally crashing test thread '%s', the following dump is expected\n", kTaskName);
    ASSERT_OK(mini_process_cmd_send(command_channel.get(), MINIP_CMD_BUILTIN_TRAP));
  }

  async::Loop loop_;
  FakeService fake_service_;

  zx::job parent_job_;
  zx::job job_;
  zx::channel exception_channel_;
};

class CrashsvcTest : public TestBase {
 public:
  CrashsvcTest() : TestBase(&kAsyncLoopConfigNoAttachToCurrentThread) { loop().StartThread(); }

  void TearDown() final {
    loop().Shutdown();

    // Kill |job_| to stop exception handling and crashsvc
    ASSERT_OK(job().kill());
    if (crashsvc_thread_.has_value()) {
      int exit_code = -1;
      ASSERT_EQ(thrd_join(*crashsvc_thread_, &exit_code), thrd_success);
      ASSERT_EQ(exit_code, 0);
    }
  }

  void StartCrashsvc(const zx_status_t svc_channel = ZX_HANDLE_INVALID) {
    zx::job job_copy;
    ASSERT_OK(job().duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy));

    thrd_t& thread = crashsvc_thread_.emplace();
    ASSERT_OK(start_crashsvc(std::move(job_copy), svc_channel, &thread));
  }

  void StartCrashsvc(const zx::channel& svc_channel) { StartCrashsvc(svc_channel.get()); }

  zx_status_t CheckPendingException(const zx::duration limit) const {
    const auto exception = ReadPendingException(limit);
    return exception.status_value();
  }

 private:
  std::optional<thrd_t> crashsvc_thread_{std::nullopt};
};

TEST_F(CrashsvcTest, StartAndStop) { ASSERT_NO_FATAL_FAILURE(StartCrashsvc()); }

TEST_F(CrashsvcTest, ThreadCrashNoExceptionHandler) {
  // crashsvc should pass the exception up the chain when done.
  ASSERT_NO_FATAL_FAILURE(StartCrashsvc());
  ASSERT_NO_FATAL_FAILURE(GenerateException());
  ASSERT_OK(CheckPendingException(zx::duration::infinite()));
}

TEST_F(CrashsvcTest, ThreadBacktraceNoExceptionHandler) {
  // The backtrace request exception should not make it out of crashsvc.
  ASSERT_NO_FATAL_FAILURE(StartCrashsvc());
  ASSERT_NO_FATAL_FAILURE(GenerateBacktraceRequest());
  ASSERT_EQ(CheckPendingException(zx::sec(0)), ZX_ERR_TIMED_OUT);
}

TEST_F(CrashsvcTest, ExceptionHandlerSuccess) {
  // crashsvc should pass the exception to the stub handler.
  ASSERT_NO_FATAL_FAILURE(StartCrashsvc(svc_channel()));
  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  EXPECT_EQ(stub_handler().exception_count(), 1);
}

TEST_F(CrashsvcTest, ExceptionHandlerAsync) {
  // We tell the stub exception handler to not respond immediately to test that this does not block
  // crashsvc from further processing other exceptions.
  stub_handler().SetRespondSync(false);

  ASSERT_NO_FATAL_FAILURE(StartCrashsvc(svc_channel()));

  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  EXPECT_EQ(stub_handler().exception_count(), 4);

  // We now tell the stub exception handler to respond all the pending requests it had, which would
  // trigger the (empty) callbacks in crashsvc on the next async loop run.
  stub_handler().SendAsyncResponses();
}

TEST_F(CrashsvcTest, MultipleThreadExceptionHandler) {
  StartCrashsvc(svc_channel());

  // Make sure crashsvc continues to loop no matter what the exception handler does.
  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  ASSERT_NO_FATAL_FAILURE(AnalyzeCrash());
  EXPECT_EQ(stub_handler().exception_count(), 4);
}

TEST_F(CrashsvcTest, ThreadBacktraceExceptionHandler) {
  // Thread backtrace requests shouldn't be sent out to the exception handler.
  ASSERT_NO_FATAL_FAILURE(StartCrashsvc(svc_channel()));
  ASSERT_NO_FATAL_FAILURE(GenerateBacktraceRequest());
  EXPECT_EQ(stub_handler().exception_count(), 0);
}

constexpr auto kExceptionHandlerTimeout = zx::sec(3);

class ExceptionHandlerTest : public TestBase {
 public:
  ExceptionHandlerTest() : TestBase(&kAsyncLoopConfigAttachToCurrentThread) {}

  void RunLoopUntil(fit::function<bool()> condition) {
    while (!condition()) {
      loop().Run(zx::deadline_after(zx::msec(10)));
    }
  }

  void RunLoopFor(zx::duration timeout) {
    while (timeout > zx::nsec(0)) {
      loop().Run(zx::deadline_after(zx::msec(10)));
      timeout -= zx::msec(10);
    }
  }
};

TEST_F(ExceptionHandlerTest, ExceptionHandlerReconnects) {
  ExceptionHandler handler(loop().dispatcher(), svc_channel().get(), kExceptionHandlerTimeout);

  RunLoopUntil([&stub = stub_handler()] { return stub.HasClient(); });
  ASSERT_TRUE(stub_handler().HasClient());

  // Simulates crashsvc losing connection with fuchsia.exception.Handler.
  ASSERT_OK(stub_handler().Unbind());

  RunLoopUntil([&handler] { return !handler.ConnectedToServer(); });
  ASSERT_FALSE(stub_handler().HasClient());

  // Create an invalid exception to trigger the reconnection logic.
  handler.Handle(zx::exception{}, zx_exception_info_t{});

  RunLoopUntil([&stub = stub_handler()] { return stub.HasClient(); });
  ASSERT_TRUE(stub_handler().HasClient());
}

TEST_F(ExceptionHandlerTest, ExceptionHandlerWaitsForIsActive) {
  ExceptionHandler handler(loop().dispatcher(), svc_channel().get(), kExceptionHandlerTimeout);

  zx::channel exception_channel_self;
  ASSERT_OK(zx::job::default_job()->create_exception_channel(0, &exception_channel_self));

  // Instructs the stub to not respond to calls to IsActive.
  stub_handler().SetIsActive(false);

  RunLoopUntil([&stub = stub_handler()] { return stub.HasClient(); });
  ASSERT_TRUE(stub_handler().HasClient());

  ASSERT_NO_FATAL_FAILURE(GenerateException());

  auto exception = ReadPendingException(zx::duration::infinite());
  ASSERT_OK(exception);

  // Handle the exception.
  handler.Handle(std::move(exception->first), exception->second);
  ASSERT_EQ(stub_handler().exception_count(), 0u);

  stub_handler().SetIsActive(true);
  RunLoopUntil([&stub = stub_handler()] { return stub.exception_count() == 1; });
}

TEST_F(ExceptionHandlerTest, ExceptionHandlerIsActiveTimeOut) {
  ExceptionHandler handler(loop().dispatcher(), svc_channel().get(), kExceptionHandlerTimeout);

  zx::channel exception_channel_self;
  ASSERT_OK(zx::job::default_job()->create_exception_channel(0, &exception_channel_self));

  // Instructs the stub to not respond to calls to IsActive.
  stub_handler().SetIsActive(false);

  RunLoopUntil([&stub = stub_handler()] { return stub.HasClient(); });
  ASSERT_TRUE(stub_handler().HasClient());

  ASSERT_NO_FATAL_FAILURE(GenerateException());

  auto exception = ReadPendingException(zx::duration::infinite());
  ASSERT_OK(exception);

  // Handle the exception.
  handler.Handle(std::move(exception->first), exception->second);

  RunLoopFor(kExceptionHandlerTimeout);
  ASSERT_EQ(stub_handler().exception_count(), 0u);

  // The exception should be passed up the chain after the timeout. Once we get the exception, kill
  // the job which will stop exception handling and cause the crashsvc thread to exit.
  //
  // Use a non-inifinte timeout to prevent hangs.
  ASSERT_OK(exception_channel_self.wait_one(ZX_CHANNEL_READABLE, zx::deadline_after(zx::sec(3)),
                                            nullptr));
}

}  // namespace
