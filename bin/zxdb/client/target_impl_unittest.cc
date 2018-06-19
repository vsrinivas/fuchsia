// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "garnet/lib/debug_ipc/helper/test_stream_buffer.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

using debug_ipc::MessageLoop;

class TargetSink : public RemoteAPI {
 public:
  TargetSink() = default;
  ~TargetSink() = default;

  void set_launch_err(const Err& err) { launch_err_ = err; }
  void set_launch_reply(const debug_ipc::LaunchReply& reply) {
    launch_reply_ = reply;
  }

  void set_attach_err(const Err& err) { attach_err_ = err; }
  void set_attach_reply(const debug_ipc::AttachReply& reply) {
    attach_reply_ = reply;
  }

  void Launch(
      const debug_ipc::LaunchRequest& request,
      std::function<void(const Err&, debug_ipc::LaunchReply)> cb) override {
    MessageLoop::Current()->PostTask(
        [this, cb]() { cb(launch_err_, launch_reply_); });
  }

  void Kill(const debug_ipc::KillRequest& request,
            std::function<void(const Err&, debug_ipc::KillReply)> cb) override {
    MessageLoop::Current()->PostTask([cb]() {
      // For now, always report success.
      cb(Err(), debug_ipc::KillReply());
    });
  }

  void Attach(
      const debug_ipc::AttachRequest& request,
      std::function<void(const Err&, debug_ipc::AttachReply)> cb) override {
    MessageLoop::Current()->PostTask([this, cb]() {
      cb(attach_err_, attach_reply_);
    });
  }

  void Detach(
      const debug_ipc::DetachRequest& request,
      std::function<void(const Err&, debug_ipc::DetachReply)> cb) override {
    MessageLoop::Current()->PostTask([cb]() {
      // For now, always report success.
      cb(Err(), debug_ipc::DetachReply());
    });
  }

 private:
  // These two variables are returned from Launch().
  Err launch_err_;
  debug_ipc::LaunchReply launch_reply_;

  // These two variables are returned from Attach().
  Err attach_err_;
  debug_ipc::AttachReply attach_reply_;
};

class TargetImplTest : public testing::Test {
 public:
  TargetImplTest() {
    loop_.Init();
    sink_ = new TargetSink;
    session_ = std::make_unique<Session>(std::unique_ptr<RemoteAPI>(sink_));
  }
  ~TargetImplTest() {
    session_.reset();
    loop_.Cleanup();
  }

  debug_ipc::PlatformMessageLoop& loop() { return loop_; }
  debug_ipc::TestStreamBuffer& stream() { return stream_; }
  TargetSink& sink() { return *sink_; }
  Session& session() { return *session_; }

 private:
  debug_ipc::PlatformMessageLoop loop_;
  debug_ipc::TestStreamBuffer stream_;

  TargetSink* sink_;  // Owned by the session_.

  std::unique_ptr<Session> session_;
};

}  // namespace

// Tests that the process state is updated when trying to launch with no
// connection to the remote system.
TEST_F(TargetImplTest, LaunchNoConnection) {
  auto target_impls = session().system_impl().GetTargetImpls();
  ASSERT_EQ(1u, target_impls.size());
  TargetImpl* target = target_impls[0];

  Err expected_err(ErrType::kNoConnection, "No conncetion.");
  sink().set_launch_err(expected_err);

  Err out_err;
  target->SetArgs(std::vector<std::string>({"foo_test", "--arg"}));
  target->Launch([&out_err](fxl::WeakPtr<Target> target, const Err& err) {
    out_err = err;
    MessageLoop::Current()->QuitNow();
  });

  // Should be in the process of launching.
  EXPECT_EQ(Target::State::kStarting, target->GetState());

  loop().Run();

  // Expect the launch to have failed and be in a non-running state.
  EXPECT_EQ(expected_err, out_err);
  EXPECT_EQ(Target::State::kNone, target->GetState());
}

// Tests a successful launch and kill.
TEST_F(TargetImplTest, LaunchKill) {
  auto target_impls = session().system_impl().GetTargetImpls();
  ASSERT_EQ(1u, target_impls.size());
  TargetImpl* target = target_impls[0];

  // Specify a successful reply.
  const uint64_t kKoid = 1234;
  sink().set_launch_err(Err());
  debug_ipc::LaunchReply requested_reply;
  requested_reply.status = 0;
  requested_reply.process_koid = kKoid;
  requested_reply.process_name = "my name";
  sink().set_launch_reply(requested_reply);

  Err out_err;
  target->SetArgs(std::vector<std::string>({"foo_test", "--arg"}));
  target->Launch([&out_err](fxl::WeakPtr<Target> target, const Err& err) {
    out_err = err;
    MessageLoop::Current()->QuitNow();
  });

  // Should be in the process of launching.
  EXPECT_EQ(Target::State::kStarting, target->GetState());

  // Try to launch another one in the pending state.
  Err second_launch_err;
  target->Launch([&second_launch_err](fxl::WeakPtr<Target> target,
                                      const Err& err) {
    second_launch_err = err;
    MessageLoop::Current()->QuitNow();
  });

  // We should have two tasks posted the first pending attach and the second
  // pending launch (that we expect to fail). They will both quit so the loop
  // need to be run twice.
  loop().Run();
  loop().Run();

  // Expect good launch, it should have made a process.
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(Target::State::kRunning, target->GetState());
  ASSERT_TRUE(target->GetProcess());
  EXPECT_EQ(kKoid, target->GetProcess()->GetKoid());

  // The second launch should have failed.
  EXPECT_TRUE(second_launch_err.has_error());

  // Should not be able to launch another one. It should error out before
  // trying to send IPC.
  target->Launch([&out_err](fxl::WeakPtr<Target> target, const Err& err) {
    out_err = err;
    MessageLoop::Current()->QuitNow();
  });

  loop().Run();
  EXPECT_TRUE(out_err.has_error());

  // Should not be able to attach.
  target->Attach(1234, [&out_err](fxl::WeakPtr<Target> target, const Err& err) {
    out_err = err;
    MessageLoop::Current()->QuitNow();
  });

  loop().Run();
  EXPECT_TRUE(out_err.has_error());

  // Kill the process.
  target->Kill([&out_err](fxl::WeakPtr<Target> target, const Err& err) {
    out_err = err;
    MessageLoop::Current()->QuitNow();
  });
  loop().Run();

  // Should have succeeded.
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(Target::State::kNone, target->GetState());
}

// Tests a successful attach and detach.
TEST_F(TargetImplTest, AttachDetach) {
  auto target_impls = session().system_impl().GetTargetImpls();
  ASSERT_EQ(1u, target_impls.size());
  TargetImpl* target = target_impls[0];

  // Specify a successful reply.
  const uint64_t kKoid = 1234;
  sink().set_attach_err(Err());

  Err out_err;
  target->Attach(kKoid,
                 [&out_err](fxl::WeakPtr<Target> target, const Err& err) {
                   out_err = err;
                   MessageLoop::Current()->QuitNow();
                 });

  // Should be in the process of launching.
  EXPECT_EQ(Target::State::kAttaching, target->GetState());

  // Try to launch another one in the pending state.
  Err second_launch_err;
  target->Launch([&second_launch_err](fxl::WeakPtr<Target> target,
                                      const Err& err) {
    second_launch_err = err;
    MessageLoop::Current()->QuitNow();
  });

  // We should have two tasks posted the first pending attach and the second
  // pending launch (that we expect to fail). They will both quit so the loop
  // need to be run twice.
  loop().Run();
  loop().Run();

  // Expect good attach, it should have made a process.
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(Target::State::kRunning, target->GetState());
  ASSERT_TRUE(target->GetProcess());
  EXPECT_EQ(kKoid, target->GetProcess()->GetKoid());

  // The second we launch should have failed.
  EXPECT_TRUE(second_launch_err.has_error());

  // Detach.
  target->Detach([&out_err](fxl::WeakPtr<Target> target, const Err& err) {
    out_err = err;
    MessageLoop::Current()->QuitNow();
  });
  loop().Run();

  // Should be in a non-running state.
  EXPECT_EQ(Target::State::kNone, target->GetState());
}

}  // namespace zxdb
