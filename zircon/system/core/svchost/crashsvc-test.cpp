// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <crashsvc/crashsvc.h>

#include <threads.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <memory>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/crash/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-async/bind.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
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

// Creates a mini-process under |job| and tells it to crash.
void CreateAndCrashProcess(const zx::job& job, zx::process* process, zx::thread* thread) {
    zx::vmar vmar;
    constexpr char kTaskName[] = "crashsvc-test";
    constexpr uint32_t kTaskNameLen = sizeof(kTaskName) - 1;
    ASSERT_OK(zx::process::create(job, kTaskName, kTaskNameLen, 0, process, &vmar));
    ASSERT_OK(zx::thread::create(*process, kTaskName, kTaskNameLen, 0, thread));

    zx::event event;
    ASSERT_OK(zx::event::create(0, &event));

    zx::channel command_channel;
    ASSERT_OK(start_mini_process_etc(process->get(), thread->get(), vmar.get(), event.release(),
                                     command_channel.reset_and_get_address()));

    printf("Intentionally crashing test thread '%s', the following dump is expected\n", kTaskName);
    ASSERT_OK(mini_process_cmd_send(command_channel.get(), MINIP_CMD_BUILTIN_TRAP));
}

TEST(crashsvc, ThreadCrashNoAnalyzer) {
    zx::job job;
    ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &job));

    // Catch exceptions on our job so that the crashing thread doesn't go all
    // the way up to the system crashsvc.
    zx::channel exception_channel;
    ASSERT_OK(job.create_exception_channel(0, &exception_channel));

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

// Returns the object's koid, or ZX_KOID_INVALID and marks test failure if
// get_info() fails.
template <typename T>
zx_koid_t GetKoid(const zx::object<T>& object) {
    zx_info_handle_basic_t info;
    info.koid = ZX_KOID_INVALID;
    EXPECT_OK(object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    return info.koid;
}

// Provides FIDL stubs for fuchsia::crash::Analyzer.
class CrashAnalyzerStub {
public:
    enum class Behavior {
        kResumeThread, // Resume exception handling and return ZX_OK.
        kError         // Simulate analyzer failure by returning an error.
    };

    // Sets the behavior to use on the next OnNativeException() call. |process|
    // and |thread| are the tasks we expect to be given from crashsvc.
    void SetBehavior(Behavior behavior, const zx::process& process, const zx::thread& thread) {
        behavior_ = behavior;
        process_koid_ = GetKoid(process);
        thread_koid_ = GetKoid(thread);
        ASSERT_NE(process_koid_, ZX_KOID_INVALID);
        ASSERT_NE(thread_koid_, ZX_KOID_INVALID);
    }

    // Creates a virtual file system serving this analyzer at the appropriate path.
    void Serve(async_dispatcher_t* dispatcher, std::unique_ptr<fs::SynchronousVfs>* vfs,
               zx::channel* client) {
        auto directory = fbl::MakeRefCounted<fs::PseudoDir>();
        auto node = fbl::MakeRefCounted<fs::Service>([dispatcher, this](zx::channel channel) {
            auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_crash_Analyzer_dispatch);
            return fidl_bind(dispatcher, channel.release(), dispatch, this,
                             &CrashAnalyzerStub::kOps);
        });
        ASSERT_OK(directory->AddEntry(fuchsia_crash_Analyzer_Name, std::move(node)));

        zx::channel server;
        ASSERT_OK(zx::channel::create(0u, client, &server));

        *vfs = std::make_unique<fs::SynchronousVfs>(dispatcher);
        ASSERT_OK((*vfs)->ServeDirectory(std::move(directory), std::move(server)));
    }

private:
    static zx_status_t OnNativeExceptionWrapper(void* ctx, zx_handle_t process, zx_handle_t thread,
                                                zx_handle_t exception_port, fidl_txn_t* txn) {
        return reinterpret_cast<CrashAnalyzerStub*>(ctx)->OnNativeException(
            zx::process(process), zx::thread(thread), zx::port(exception_port), txn);
    }

    zx_status_t OnNativeException(zx::process process, zx::thread thread, zx::port exception_port,
                                  fidl_txn_t* txn) {
        // Make sure crashsvc passed us the correct task handles.
        EXPECT_EQ(process_koid_, GetKoid(process));
        EXPECT_EQ(thread_koid_, GetKoid(thread));

        // Build a reply corresponding to our desired behavior.
        fuchsia_crash_Analyzer_OnNativeException_Result result;
        if (behavior_ == Behavior::kResumeThread) {
            EXPECT_OK(thread.resume_from_exception(exception_port, ZX_RESUME_TRY_NEXT));
            result.tag = fuchsia_crash_Analyzer_OnNativeException_ResultTag_response;
        } else {
            result.tag = fuchsia_crash_Analyzer_OnNativeException_ResultTag_err;
            result.err = ZX_ERR_BAD_STATE;
        }

        zx_status_t status = fuchsia_crash_AnalyzerOnNativeException_reply(txn, &result);
        EXPECT_EQ(status, ZX_OK);
        return status;
    }

    static constexpr fuchsia_crash_Analyzer_ops_t kOps = {
        .OnNativeException = OnNativeExceptionWrapper,
        .OnManagedRuntimeException = nullptr,
        .OnKernelPanicCrashLog = nullptr};

    Behavior behavior_;
    zx_koid_t process_koid_ = ZX_KOID_INVALID;
    zx_koid_t thread_koid_ = ZX_KOID_INVALID;
};

// Creates a new thread, crashes it, and processes the resulting Analyzer FIDL
// message from crashsvc according to |behavior|.
void AnalyzeCrash(CrashAnalyzerStub* analyzer, async::Loop* loop, const zx::job& job,
                  CrashAnalyzerStub::Behavior behavior) {
    zx::channel exception_channel;
    ASSERT_OK(job.create_exception_channel(0, &exception_channel));

    zx::process process;
    zx::thread thread;
    ASSERT_NO_FATAL_FAILURES(CreateAndCrashProcess(job, &process, &thread));

    ASSERT_NO_FATAL_FAILURES(analyzer->SetBehavior(behavior, process, thread));

    // Run the loop until the exception filters up to our job handler.
    async::Wait wait(exception_channel.get(), ZX_CHANNEL_READABLE, [&loop](...) {
        loop->Quit();
    });
    ASSERT_OK(wait.Begin(loop->dispatcher()));
    ASSERT_EQ(loop->Run(), ZX_ERR_CANCELED);
    ASSERT_OK(loop->ResetQuit());

    // Kill the process so it doesn't bubble up to the real crashsvc when we
    // close our exception channel.
    ASSERT_OK(process.kill());
    ASSERT_OK(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr));
}

TEST(crashsvc, ThreadCrashAnalyzerSuccess) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    std::unique_ptr<fs::SynchronousVfs> vfs;
    zx::channel client;
    CrashAnalyzerStub analyzer;
    ASSERT_NO_FATAL_FAILURES(analyzer.Serve(loop.dispatcher(), &vfs, &client));

    zx::job job;
    zx::job job_copy;
    thrd_t cthread;
    ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &job));
    ASSERT_OK(job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy));
    ASSERT_OK(start_crashsvc(std::move(job_copy), client.get(), &cthread));

    ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&analyzer, &loop, job,
                                          CrashAnalyzerStub::Behavior::kResumeThread));

    ASSERT_OK(job.kill());
    EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

TEST(crashsvc, ThreadCrashAnalyzerFailure) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    std::unique_ptr<fs::SynchronousVfs> vfs;
    zx::channel client;
    CrashAnalyzerStub analyzer;
    ASSERT_NO_FATAL_FAILURES(analyzer.Serve(loop.dispatcher(), &vfs, &client));

    zx::job job;
    zx::job job_copy;
    thrd_t cthread;
    ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &job));
    ASSERT_OK(job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy));
    ASSERT_OK(start_crashsvc(std::move(job_copy), client.get(), &cthread));

    ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&analyzer, &loop, job,
                                          CrashAnalyzerStub::Behavior::kError));

    ASSERT_OK(job.kill());
    EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

TEST(crashsvc, MultipleThreadCrashAnalyzer) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    std::unique_ptr<fs::SynchronousVfs> vfs;
    zx::channel client;
    CrashAnalyzerStub analyzer;
    ASSERT_NO_FATAL_FAILURES(analyzer.Serve(loop.dispatcher(), &vfs, &client));

    zx::job job;
    zx::job job_copy;
    thrd_t cthread;
    ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &job));
    ASSERT_OK(job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy));
    ASSERT_OK(start_crashsvc(std::move(job_copy), client.get(), &cthread));

    // Make sure crashsvc continues to loop no matter what the analyzer does.
    ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&analyzer, &loop, job,
                                          CrashAnalyzerStub::Behavior::kResumeThread));
    ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&analyzer, &loop, job,
                                          CrashAnalyzerStub::Behavior::kError));
    ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&analyzer, &loop, job,
                                          CrashAnalyzerStub::Behavior::kResumeThread));
    ASSERT_NO_FATAL_FAILURES(AnalyzeCrash(&analyzer, &loop, job,
                                          CrashAnalyzerStub::Behavior::kError));

    ASSERT_OK(job.kill());
    EXPECT_EQ(thrd_join(cthread, nullptr), thrd_success);
}

} // namespace
