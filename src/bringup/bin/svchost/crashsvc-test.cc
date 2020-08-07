// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/exception/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <threads.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>

#include <list>
#include <memory>

#include <crashsvc/crashsvc.h>
#include <crashsvc/exception_handler.h>
#include <fs/pseudo_dir.h>
#include <fs/service.h>
#include <fs/synchronous_vfs.h>
#include <mini-process/mini-process.h>
#include <zxtest/zxtest.h>

namespace {

TEST(crashsvc, StartAndStop) {
  zx::job job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &job));

  thrd_t thread;
  zx::job job_copy;
  ASSERT_OK(job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy));
  ASSERT_OK(start_crashsvc(std::move(job_copy), ZX_HANDLE_INVALID, &thread));

  ASSERT_OK(job.kill());

  int exit_code = -1;
  EXPECT_EQ(thrd_join(thread, &exit_code), thrd_success);
  EXPECT_EQ(exit_code, 0);
}

constexpr char kTaskName[] = "crashsvc-test";
constexpr uint32_t kTaskNameLen = sizeof(kTaskName) - 1;

// Creates a mini-process under |job|.
void CreateMiniProcess(const zx::job& job, zx::process* process, zx::thread* thread,
                       zx::channel* command_channel) {
  zx::vmar vmar;
  ASSERT_OK(zx::process::create(job, kTaskName, kTaskNameLen, 0, process, &vmar));
  ASSERT_OK(zx::thread::create(*process, kTaskName, kTaskNameLen, 0, thread));

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  ASSERT_OK(start_mini_process_etc(process->get(), thread->get(), vmar.get(), event.release(), true,
                                   command_channel->reset_and_get_address()));
}

// Creates a mini-process under |job| and tells it to crash.
void CreateAndCrashProcess(const zx::job& job, zx::process* process, zx::thread* thread) {
  zx::channel command_channel;
  ASSERT_NO_FATAL_FAILURES(CreateMiniProcess(job, process, thread, &command_channel));

  // Use mini_process_cmd_send() here to send but not wait for a response
  // so we can handle the exception.
  printf("Intentionally crashing test thread '%s', the following dump is expected\n", kTaskName);
  ASSERT_OK(mini_process_cmd_send(command_channel.get(), MINIP_CMD_BUILTIN_TRAP));
}

// Creates a mini-process under |job| and tells it to request a backtrace.
// Blocks until the mini-process thread has successfully resumed.
void CreateAndBacktraceProcess(const zx::job& job, zx::process* process, zx::thread* thread) {
  zx::channel command_channel;
  ASSERT_NO_FATAL_FAILURES(CreateMiniProcess(job, process, thread, &command_channel));

  // Use mini_process_cmd() here to send and block until we get a response.
  printf("Intentionally dumping test thread '%s', the following dump is expected\n", kTaskName);
  ASSERT_OK(mini_process_cmd(command_channel.get(), MINIP_CMD_BACKTRACE_REQUEST, nullptr));
}

TEST(crashsvc, ThreadCrashNoExceptionHandler) {
  zx::job parent_job, job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &parent_job));
  ASSERT_OK(zx::job::create(parent_job, 0, &job));

  // Catch exceptions on |parent_job| so that the crashing thread doesn't go
  // all the way up to the system crashsvc when our local crashsvc is done.
  zx::channel exception_channel;
  ASSERT_OK(parent_job.create_exception_channel(0, &exception_channel));

  thrd_t cthread;
  zx::job job_copy;
  ASSERT_OK(job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy));
  ASSERT_OK(start_crashsvc(std::move(job_copy), ZX_HANDLE_INVALID, &cthread));

  zx::process process;
  zx::thread thread;
  ASSERT_NO_FATAL_FAILURES(CreateAndCrashProcess(job, &process, &thread));

  // crashsvc should pass exception handling up the chain when done. Once we
  // get the exception, kill the job which will stop exception handling and
  // cause the crashsvc thread to exit.
  ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr));
  ASSERT_OK(job.kill());
  EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

TEST(crashsvc, ThreadBacktraceNoExceptionHandler) {
  zx::job parent_job, job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &parent_job));
  ASSERT_OK(zx::job::create(parent_job, 0, &job));

  zx::channel exception_channel;
  ASSERT_OK(parent_job.create_exception_channel(0, &exception_channel));

  thrd_t cthread;
  zx::job job_copy;
  ASSERT_OK(job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy));
  ASSERT_OK(start_crashsvc(std::move(job_copy), ZX_HANDLE_INVALID, &cthread));

  zx::process process;
  zx::thread thread;
  ASSERT_NO_FATAL_FAILURES(CreateAndBacktraceProcess(job, &process, &thread));

  // The backtrace request exception should not make it out of crashsvc.
  ASSERT_EQ(exception_channel.wait_one(ZX_CHANNEL_READABLE, zx::time(0), nullptr),
            ZX_ERR_TIMED_OUT);
  ASSERT_OK(job.kill());
  EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

// Creates a new thread, crashes it, and processes the resulting ExceptionHandler FIDL
// message from crashsvc according to |behavior|.
//
// |parent_job| is used to catch exceptions after they've been analyzed on |job|
// so that they don't bubble up to the real crashsvc.
void AnalyzeCrash(async::Loop* loop, const zx::job& parent_job, const zx::job& job) {
  zx::channel exception_channel;
  ASSERT_OK(parent_job.create_exception_channel(0, &exception_channel));

  zx::process process;
  zx::thread thread;
  ASSERT_NO_FATAL_FAILURES(CreateAndCrashProcess(job, &process, &thread));

  // Run the loop until the exception filters up to our job handler.
  async::Wait wait(exception_channel.get(), ZX_CHANNEL_READABLE, 0, [&loop](...) { loop->Quit(); });
  ASSERT_OK(wait.Begin(loop->dispatcher()));
  ASSERT_EQ(loop->Run(), ZX_ERR_CANCELED);
  ASSERT_OK(loop->ResetQuit());

  // The exception is now waiting in |exception_channel|, kill the process
  // before the channel closes to keep it from propagating further.
  ASSERT_OK(process.kill());
  ASSERT_OK(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr));
}

// Crashsvc will attemp to connect to a |fuchsia.exception.Handler| when it catches an exception.
// We use this fake in order to verify that behaviour.
class StubExceptionHandler final : public llcpp::fuchsia::exception::Handler::Interface {
 public:
  zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
    auto binding = fidl::BindServer(dispatcher, std::move(request), this);
    if (binding.is_error()) {
      return binding.error();
    }
    binding_ = binding.take_value();
    return ZX_OK;
  }

  // fuchsia.exception.Handler
  void OnException(::zx::exception exception, llcpp::fuchsia::exception::ExceptionInfo info,
                   OnExceptionCompleter::Sync completer) override {
    exception_count_++;
    if (respond_sync_) {
      completer.Reply();
    } else {
      completers_.push_back(completer.ToAsync());
    }
  }

  void SendAsyncResponses() {
    for (auto& completer : completers_) {
      completer.Reply();
    }

    completers_.clear();
  }

  void SetRespondSync(bool val) { respond_sync_ = true; }

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
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::exception::Handler>> binding_;

  int exception_count_ = 0;
  bool respond_sync_{true};
  std::list<OnExceptionCompleter::Async> completers_;
};

// Exposes the services through a virtual directory that crashsvc uses in order to connect to
// services. We use this to inject a |StubExceptionHandler| for the |fuchsia.exception.Handler|
// service.
class FakeService {
 public:
  FakeService(async_dispatcher_t* dispatcher) : vfs_(dispatcher) {
    auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
    root_dir->AddEntry(llcpp::fuchsia::exception::Handler::Name,
                       fbl::MakeRefCounted<fs::Service>([this, dispatcher](zx::channel request) {
                         return exception_handler_.Connect(dispatcher, std::move(request));
                       }));

    // We serve this directory.
    zx::channel svc_remote;
    ASSERT_OK(zx::channel::create(0, &svc_local_, &svc_remote));
    vfs_.ServeDirectory(root_dir, std::move(svc_remote));
  }

  StubExceptionHandler& exception_handler() { return exception_handler_; }
  const zx::channel& service_channel() const { return svc_local_; }

 private:
  fs::SynchronousVfs vfs_;
  StubExceptionHandler exception_handler_;
  zx::channel svc_local_;
};

// Creates a sub-job under the current one to be used as a realm for the processes that will be
// spawned for tests.
struct Jobs {
  zx::job parent_job;  // The job of this test.
  zx::job job;         // The job under which the process will be created.
  zx::job job_copy;
};

void GetTestJobs(Jobs* jobs) {
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &jobs->parent_job));
  ASSERT_OK(zx::job::create(jobs->parent_job, 0, &jobs->job));
  ASSERT_OK(jobs->job.duplicate(ZX_RIGHT_SAME_RIGHTS, &jobs->job_copy));
}

TEST(crashsvc, ExceptionHandlerSuccess) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  FakeService test_svc(loop.dispatcher());

  Jobs jobs;
  ASSERT_NO_FATAL_FAILURES(GetTestJobs(&jobs));

  // Start crashsvc.
  thrd_t cthread;
  ASSERT_OK(start_crashsvc(std::move(jobs.job_copy), test_svc.service_channel().get(), &cthread));

  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  EXPECT_EQ(test_svc.exception_handler().exception_count(), 1);

  // Kill the test job so that the exception doesn't bubble outside of this test.
  ASSERT_OK(jobs.job.kill());
  EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

TEST(crashsvc, ExceptionHandlerAsync) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  FakeService test_svc(loop.dispatcher());

  Jobs jobs;
  ASSERT_NO_FATAL_FAILURES(GetTestJobs(&jobs));

  // We tell the stub exception handler to not respond immediately to test that this does not block
  // crashsvc from further processing other exceptions.
  test_svc.exception_handler().SetRespondSync(false);

  // Start crashsvc.
  thrd_t cthread;
  ASSERT_OK(start_crashsvc(std::move(jobs.job_copy), test_svc.service_channel().get(), &cthread));

  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  EXPECT_EQ(test_svc.exception_handler().exception_count(), 4);

  // We now tell the stub exception handler to respond all the pending requests it had, which would
  // trigger the (empty) callbacks in crashsvc on the next async loop run.
  test_svc.exception_handler().SendAsyncResponses();

  // Kill the test job so that the exception doesn't bubble outside of this test.
  ASSERT_OK(jobs.job.kill());
  EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

TEST(crashsvc, MultipleThreadExceptionHandler) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  FakeService test_svc(loop.dispatcher());

  Jobs jobs;
  ASSERT_NO_FATAL_FAILURES(GetTestJobs(&jobs));

  // Start crashsvc.
  thrd_t cthread;
  ASSERT_OK(start_crashsvc(std::move(jobs.job_copy), test_svc.service_channel().get(), &cthread));

  // Make sure crashsvc continues to loop no matter what the exception handler does.
  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&loop, jobs.parent_job, jobs.job));
  EXPECT_EQ(test_svc.exception_handler().exception_count(), 4);

  // Kill the test job so that the exception doesn't bubble outside of this test.
  ASSERT_OK(jobs.job.kill());
  EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

TEST(crashsvc, ThreadBacktraceExceptionHandler) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  FakeService test_svc(loop.dispatcher());

  Jobs jobs;
  ASSERT_NO_FATAL_FAILURES(GetTestJobs(&jobs));

  // Start crashsvc.
  thrd_t cthread;
  ASSERT_OK(start_crashsvc(std::move(jobs.job_copy), test_svc.service_channel().get(), &cthread));

  // Creates a process that triggers the backtrace request.
  zx::process process;
  zx::thread thread;
  ASSERT_NO_FATAL_FAILURES(CreateAndBacktraceProcess(jobs.job, &process, &thread));

  // Thread backtrace requests shouldn't be sent out to the exception handler.
  EXPECT_EQ(test_svc.exception_handler().exception_count(), 0);

  // Kill the test job so that the exception doesn't bubble outside of this test.
  ASSERT_OK(jobs.job.kill());
  EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

TEST(ExceptionHandlerTest, ExceptionHandlerReconnects) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto RunUntil = [&loop](fit::function<bool()> condition) {
    while (!condition()) {
      loop.Run(zx::deadline_after(zx::msec(10)));
    }
  };

  FakeService test_svc(loop.dispatcher());

  ExceptionHandler handler(loop.dispatcher(), test_svc.service_channel().get());

  RunUntil([&test_svc] { return test_svc.exception_handler().HasClient(); });
  ASSERT_TRUE(test_svc.exception_handler().HasClient());

  // Simulates crashsvc losing connection with fuchsia.exception.Handler.
  ASSERT_OK(test_svc.exception_handler().Unbind());

  RunUntil([&handler] { return !handler.ConnectedToServer(); });
  ASSERT_FALSE(test_svc.exception_handler().HasClient());

  // Create an invalid exception to trigger the reconnection logic.
  handler.Handle(zx::exception{}, zx_exception_info_t{});

  RunUntil([&test_svc] { return test_svc.exception_handler().HasClient(); });
  ASSERT_TRUE(test_svc.exception_handler().HasClient());

  loop.Shutdown();
}

}  // namespace
