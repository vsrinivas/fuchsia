// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>

#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/common/host_util.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

class MinidumpTest : public testing::Test {
 public:
  MinidumpTest();
  virtual ~MinidumpTest();

  debug_ipc::PlatformMessageLoop& loop() { return loop_; }
  Session& session() { return *session_; }

  Err TryOpen(const std::string& filename);

  template <typename RequestType, typename ReplyType>
  void DoRequest(
      RequestType request, ReplyType& reply, Err& err,
      void (RemoteAPI::*handler)(const RequestType&,
                                 std::function<void(const Err&, ReplyType)>));

 private:
  debug_ipc::PlatformMessageLoop loop_;
  std::unique_ptr<Session> session_;
};

MinidumpTest::MinidumpTest() {
  loop_.Init();
  session_ = std::make_unique<Session>();
}

MinidumpTest::~MinidumpTest() { loop_.Cleanup(); }

Err MinidumpTest::TryOpen(const std::string& filename) {
  static auto data_dir = std::filesystem::path(GetSelfPath())
    .parent_path().parent_path() / "test_data" / "zxdb";

  Err err;
  auto path = (data_dir / filename).string();

  session().OpenMinidump(path,
                         [&err](const Err& got) {
                           err = got;
                           debug_ipc::MessageLoop::Current()->QuitNow();
                         });

  loop().Run();

  return err;
}

template<typename RequestType, typename ReplyType>
void MinidumpTest::DoRequest(RequestType request, ReplyType& reply, Err& err,
                             void (RemoteAPI::*handler)(
                               const RequestType&,
                               std::function<void(const Err&, ReplyType)>)) {
  (session().remote_api()->*handler)(request,
    [&reply, &err](const Err& e, ReplyType r) {
      err = e;
      reply = r;
      debug_ipc::MessageLoop::Current()->QuitNow();
    });
  loop().Run();
}

#define EXPECT_ZXDB_SUCCESS(e_) \
  ({ Err e = e_; EXPECT_FALSE(e.has_error()) << e.msg(); })
#define ASSERT_ZXDB_SUCCESS(e_) \
  ({ Err e = e_; ASSERT_FALSE(e.has_error()) << e.msg(); })

constexpr uint32_t kTestExampleMinidumpKOID = 656254UL;

TEST_F(MinidumpTest, Load) {
  EXPECT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));
}

TEST_F(MinidumpTest, ProcessTreeRecord) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::ProcessTreeReply reply;
  DoRequest(debug_ipc::ProcessTreeRequest(), reply, err,
            &RemoteAPI::ProcessTree);
  ASSERT_ZXDB_SUCCESS(err);

  auto record = reply.root;
  EXPECT_EQ(debug_ipc::ProcessTreeRecord::Type::kProcess, record.type);
  EXPECT_EQ("<core dump>", record.name);
  EXPECT_EQ(kTestExampleMinidumpKOID, record.koid);
  EXPECT_EQ(0UL, record.children.size());
}

TEST_F(MinidumpTest, AttachDetach) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::AttachRequest request;
  debug_ipc::AttachReply reply;

  request.koid = kTestExampleMinidumpKOID;
  DoRequest(request, reply, err, &RemoteAPI::Attach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_EQ(0UL, reply.status);
  EXPECT_EQ("<core dump>", reply.process_name);

  debug_ipc::DetachRequest detach_request;
  debug_ipc::DetachReply detach_reply;

  detach_request.process_koid = kTestExampleMinidumpKOID;
  DoRequest(detach_request, detach_reply, err, &RemoteAPI::Detach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_EQ(0UL, detach_reply.status);

  /* Try to detach when not attached */
  DoRequest(detach_request, detach_reply, err, &RemoteAPI::Detach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_NE(0UL, detach_reply.status);
}

TEST_F(MinidumpTest, AttachFail) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::AttachRequest request;
  debug_ipc::AttachReply reply;

  request.koid = 42;
  DoRequest(request, reply, err, &RemoteAPI::Attach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_NE(0UL, reply.status);
}

}  // namespace zxdb
