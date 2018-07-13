// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/session.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

class ProcessSink : public RemoteAPI {
 public:
  ProcessSink() = default;
  ~ProcessSink() = default;

  const debug_ipc::ResumeRequest& resume_request() const {
    return resume_request_;
  }
  int resume_count() const { return resume_count_; }

  void Resume(
      const debug_ipc::ResumeRequest& request,
      std::function<void(const Err&, debug_ipc::ResumeReply)> cb) override {
    resume_count_++;
    resume_request_ = request;
    debug_ipc::MessageLoop::Current()->PostTask(
        [cb]() { cb(Err(), debug_ipc::ResumeReply()); });
  }

 private:
  debug_ipc::ResumeRequest resume_request_;
  int resume_count_ = 0;
};

class ProcessImplTest : public RemoteAPITest {
 public:
  ProcessImplTest() = default;
  ~ProcessImplTest() override = default;

  ProcessSink* sink() { return sink_; }

 private:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto sink = std::make_unique<ProcessSink>();
    sink_ = sink.get();
    return std::move(sink);
  }

 private:
  ProcessSink* sink_;  // Owned by the session.
};

}  // namespace

// Tests that the correct threads are resumed after the modules are loaded.
TEST_F(ProcessImplTest, OnModules) {
  constexpr uint64_t kProcessKoid = 1234;
  Process* process = InjectProcess(kProcessKoid);
  ASSERT_TRUE(process);

  debug_ipc::NotifyModules notify;
  notify.process_koid = kProcessKoid;
  notify.modules.resize(1);
  notify.modules[0].name = "comctl32.dll";
  notify.modules[0].base = 0x7685348234;

  constexpr uint64_t kThread1Koid = 237645;
  constexpr uint64_t kThread2Koid = 809712;
  notify.stopped_thread_koids.push_back(kThread1Koid);
  notify.stopped_thread_koids.push_back(kThread2Koid);

  session().DispatchNotifyModules(notify);

  // Should have resumed both of those threads.
  ASSERT_EQ(1, sink()->resume_count());
  const debug_ipc::ResumeRequest& resume = sink()->resume_request();
  EXPECT_EQ(kProcessKoid, resume.process_koid);
  EXPECT_EQ(debug_ipc::ResumeRequest::How::kContinue, resume.how);
  EXPECT_EQ(notify.stopped_thread_koids, resume.thread_koids);
}

}  // namespace zxdb
