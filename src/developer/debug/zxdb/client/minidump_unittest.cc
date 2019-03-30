// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>
#include <map>

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/host_util.h"

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
  static auto data_dir =
      std::filesystem::path(GetSelfPath()).parent_path() / "test_data" / "zxdb";

  Err err;
  auto path = (data_dir / filename).string();

  session().OpenMinidump(path, [&err](const Err& got) {
    err = got;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });

  loop().Run();

  return err;
}

template <typename RequestType, typename ReplyType>
void MinidumpTest::DoRequest(
    RequestType request, ReplyType& reply, Err& err,
    void (RemoteAPI::*handler)(const RequestType&,
                               std::function<void(const Err&, ReplyType)>)) {
  (session().remote_api()->*handler)(
      request, [&reply, &err](const Err& e, ReplyType r) {
        err = e;
        reply = r;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });
  loop().Run();
}

template <typename Data>
std::vector<uint8_t> AsData(Data d) {
  std::vector<uint8_t> ret;

  ret.resize(sizeof(d));

  *reinterpret_cast<Data*>(ret.data()) = d;
  return ret;
}

#define EXPECT_ZXDB_SUCCESS(e_)             \
  ({                                        \
    Err e = e_;                             \
    EXPECT_FALSE(e.has_error()) << e.msg(); \
  })
#define ASSERT_ZXDB_SUCCESS(e_)             \
  ({                                        \
    Err e = e_;                             \
    ASSERT_FALSE(e.has_error()) << e.msg(); \
  })

constexpr uint32_t kTestExampleMinidumpKOID = 656254UL;
constexpr uint32_t kTestExampleMinidumpThreadKOID = 671806UL;
constexpr uint64_t kTestExampleMinidumpStackAddr = 0x37f880947000;
constexpr uint32_t kTestExampleMinidumpStackSize = 0x40000;

constexpr uint32_t kTestExampleMinidumpWithAspaceKOID = 9462UL;

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
  EXPECT_EQ("scenic", record.name);
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

  EXPECT_EQ(0, reply.status);
  EXPECT_EQ("scenic", reply.name);

  debug_ipc::DetachRequest detach_request;
  debug_ipc::DetachReply detach_reply;

  detach_request.koid = kTestExampleMinidumpKOID;
  DoRequest(detach_request, detach_reply, err, &RemoteAPI::Detach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_EQ(0, detach_reply.status);

  /* Try to detach when not attached */
  DoRequest(detach_request, detach_reply, err, &RemoteAPI::Detach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_NE(0, detach_reply.status);
}

TEST_F(MinidumpTest, AttachFail) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::AttachRequest request;
  debug_ipc::AttachReply reply;

  request.koid = 42;
  DoRequest(request, reply, err, &RemoteAPI::Attach);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_NE(0, reply.status);
}

TEST_F(MinidumpTest, Threads) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::ThreadsRequest request;
  debug_ipc::ThreadsReply reply;

  request.process_koid = kTestExampleMinidumpKOID;
  DoRequest(request, reply, err, &RemoteAPI::Threads);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_LT(0UL, reply.threads.size());
  EXPECT_EQ(1UL, reply.threads.size());

  auto& thread = reply.threads[0];

  EXPECT_EQ(kTestExampleMinidumpThreadKOID, thread.koid);
  EXPECT_EQ("", thread.name);
  EXPECT_EQ(debug_ipc::ThreadRecord::State::kCoreDump, thread.state);
}

TEST_F(MinidumpTest, Registers) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::ReadRegistersRequest request;
  debug_ipc::ReadRegistersReply reply;

  using C = debug_ipc::RegisterCategory::Type;
  using R = debug_ipc::RegisterID;

  request.process_koid = kTestExampleMinidumpKOID;
  request.thread_koid = kTestExampleMinidumpThreadKOID;
  request.categories = {
      C::kGeneral,
      C::kFP,
      C::kVector,
      C::kDebug,
  };
  DoRequest(request, reply, err, &RemoteAPI::ReadRegisters);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_EQ(4UL, reply.categories.size());

  EXPECT_EQ(C::kGeneral, reply.categories[0].type);
  EXPECT_EQ(C::kFP, reply.categories[1].type);
  EXPECT_EQ(C::kVector, reply.categories[2].type);
  EXPECT_EQ(C::kDebug, reply.categories[3].type);

  std::map<std::pair<C, R>, std::vector<uint8_t>> got;
  for (const auto& cat : reply.categories) {
    for (const auto& reg : cat.registers) {
      got[std::pair(cat.type, reg.id)] = reg.data;
    }
  }

  std::vector<uint8_t> zero_short = {0, 0};
  std::vector<uint8_t> zero_128 = {0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0};

  EXPECT_EQ(AsData(0x83UL), got[std::pair(C::kGeneral, R::kX64_rax)]);
  EXPECT_EQ(AsData(0x2FE150062100UL), got[std::pair(C::kGeneral, R::kX64_rbx)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kGeneral, R::kX64_rcx)]);
  EXPECT_EQ(AsData(0x4DC647A67264UL), got[std::pair(C::kGeneral, R::kX64_rdx)]);
  EXPECT_EQ(AsData(0x5283B9A79945UL), got[std::pair(C::kGeneral, R::kX64_rsi)]);
  EXPECT_EQ(AsData(0x4DC647A671D8UL), got[std::pair(C::kGeneral, R::kX64_rdi)]);
  EXPECT_EQ(AsData(0x37F880986D70UL), got[std::pair(C::kGeneral, R::kX64_rbp)]);
  EXPECT_EQ(AsData(0x37F880986D48UL), got[std::pair(C::kGeneral, R::kX64_rsp)]);
  EXPECT_EQ(AsData(0x1UL), got[std::pair(C::kGeneral, R::kX64_r8)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kGeneral, R::kX64_r9)]);
  EXPECT_EQ(AsData(0x4DC647A671D8UL), got[std::pair(C::kGeneral, R::kX64_r10)]);
  EXPECT_EQ(AsData(0x83UL), got[std::pair(C::kGeneral, R::kX64_r11)]);
  EXPECT_EQ(AsData(0x2FE150077070UL), got[std::pair(C::kGeneral, R::kX64_r12)]);
  EXPECT_EQ(AsData(0x3F4C20970A28UL), got[std::pair(C::kGeneral, R::kX64_r13)]);
  EXPECT_EQ(AsData(0xFFFFFFF5UL), got[std::pair(C::kGeneral, R::kX64_r14)]);
  EXPECT_EQ(AsData(0x2FE150062138UL), got[std::pair(C::kGeneral, R::kX64_r15)]);
  EXPECT_EQ(AsData(0x4DC6479A5B1EUL), got[std::pair(C::kGeneral, R::kX64_rip)]);
  EXPECT_EQ(AsData(0x10206UL), got[std::pair(C::kGeneral, R::kX64_rflags)]);

  EXPECT_EQ(zero_short, got[std::pair(C::kFP, R::kX64_fcw)]);
  EXPECT_EQ(zero_short, got[std::pair(C::kFP, R::kX64_fsw)]);
  EXPECT_EQ(AsData('\0'), got[std::pair(C::kFP, R::kX64_ftw)]);
  EXPECT_EQ(zero_short, got[std::pair(C::kFP, R::kX64_fop)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kFP, R::kX64_fip)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kFP, R::kX64_fdp)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFP, R::kX64_st0)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFP, R::kX64_st1)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFP, R::kX64_st2)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFP, R::kX64_st3)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFP, R::kX64_st4)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFP, R::kX64_st5)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFP, R::kX64_st6)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kFP, R::kX64_st7)]);

  EXPECT_EQ(AsData(0x0U), got[std::pair(C::kVector, R::kX64_mxcsr)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm0)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm1)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm2)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm3)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm4)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm5)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm6)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm7)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm8)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm9)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm10)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm11)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm12)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm13)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm14)]);
  EXPECT_EQ(zero_128, got[std::pair(C::kVector, R::kX64_xmm15)]);

  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr0)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr1)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr2)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr3)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr6)]);
  EXPECT_EQ(AsData(0x0UL), got[std::pair(C::kDebug, R::kX64_dr7)]);
}

TEST_F(MinidumpTest, Modules) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::ModulesRequest request;
  debug_ipc::ModulesReply reply;

  request.process_koid = kTestExampleMinidumpKOID;

  DoRequest(request, reply, err, &RemoteAPI::Modules);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_EQ(17UL, reply.modules.size());

  EXPECT_EQ("scenic", reply.modules[0].name);
  EXPECT_EQ(0x5283b9a60000UL, reply.modules[0].base);
  EXPECT_EQ("10b42e8965d35e1c", reply.modules[0].build_id);

  EXPECT_EQ("libfxl_logging.so", reply.modules[1].name);
  EXPECT_EQ(0x4b3297cab000UL, reply.modules[1].base);
  EXPECT_EQ("1abfa7d0f205d62f", reply.modules[1].build_id);

  EXPECT_EQ("libfxl.so", reply.modules[2].name);
  EXPECT_EQ(0x668d303bd000UL, reply.modules[2].base);
  EXPECT_EQ("b1c0b150a904a31a", reply.modules[2].build_id);

  EXPECT_EQ("libfsl.so", reply.modules[3].name);
  EXPECT_EQ(0x590935d06000UL, reply.modules[3].base);
  EXPECT_EQ("381f2ca7be23094b", reply.modules[3].build_id);

  EXPECT_EQ("libvulkan.so", reply.modules[4].name);
  EXPECT_EQ(0x117b5412000UL, reply.modules[4].base);
  EXPECT_EQ("4fd73d409f71ae52", reply.modules[4].build_id);

  EXPECT_EQ("libmagma.so", reply.modules[5].name);
  EXPECT_EQ(0x17e7d1bef000UL, reply.modules[5].base);
  EXPECT_EQ("b25b19de123448f7", reply.modules[5].build_id);

  EXPECT_EQ("libfdio.so", reply.modules[6].name);
  EXPECT_EQ(0x6bc14ef2000UL, reply.modules[6].base);
  EXPECT_EQ("57a8cfe3e3c5f3e618dcf1ea95c2125f", reply.modules[6].build_id);

  EXPECT_EQ("libzircon.so", reply.modules[7].name);
  EXPECT_EQ(0x469a0a8cc000UL, reply.modules[7].base);
  EXPECT_EQ("e71aad2e8791e7c1c33ac16dda37994f", reply.modules[7].build_id);

  EXPECT_EQ("libasync-default.so", reply.modules[8].name);
  EXPECT_EQ(0x3051c2800000UL, reply.modules[8].base);
  EXPECT_EQ("6501f751ad90dc9259f3e54e6d375fb6", reply.modules[8].build_id);

  EXPECT_EQ("libtrace-engine.so", reply.modules[9].name);
  EXPECT_EQ(0xfd47fbc000UL, reply.modules[9].base);
  EXPECT_EQ("cc1477c6ecf592c0b0735fb6214d005a", reply.modules[9].build_id);

  EXPECT_EQ("libsyslog.so", reply.modules[10].name);
  EXPECT_EQ(0x5615f3ac000UL, reply.modules[10].base);
  EXPECT_EQ("7fc2dbd770526e2aeaa28d4c0987fd7a", reply.modules[10].build_id);

  EXPECT_EQ("libdriver.so", reply.modules[11].name);
  EXPECT_EQ(0x3b0bf8718000UL, reply.modules[11].base);
  EXPECT_EQ("21820c862662441a154cedf719ffe9d6", reply.modules[11].build_id);

  EXPECT_EQ("libc++.so.2", reply.modules[12].name);
  EXPECT_EQ(0x4bf2c6583000UL, reply.modules[12].base);
  EXPECT_EQ("e5e82a08a3201ec0", reply.modules[12].build_id);

  EXPECT_EQ("libc++abi.so.1", reply.modules[13].name);
  EXPECT_EQ(0x2aa8fa149000UL, reply.modules[13].base);
  EXPECT_EQ("b97722222ed20925", reply.modules[13].build_id);

  EXPECT_EQ("libunwind.so.1", reply.modules[14].name);
  EXPECT_EQ(0x5ac9a6da2000UL, reply.modules[14].base);
  EXPECT_EQ("5b1a853e10fb1f98", reply.modules[14].build_id);

  EXPECT_EQ("libc.so", reply.modules[15].name);
  EXPECT_EQ(0x4dc64798f000UL, reply.modules[15].base);
  EXPECT_EQ("d9a39391e6747fcd3cce958895461cc0", reply.modules[15].build_id);

  EXPECT_EQ("libframebuffer.so", reply.modules[16].name);
  EXPECT_EQ(0x5fa025a5b000UL, reply.modules[16].base);
  EXPECT_EQ("5869ebacaede36a3d43a24359083c628", reply.modules[16].build_id);
}

TEST_F(MinidumpTest, AddressSpace) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump_with_aspace.dmp"));

  Err err;
  debug_ipc::AddressSpaceRequest request;
  debug_ipc::AddressSpaceReply reply;

  request.process_koid = kTestExampleMinidumpWithAspaceKOID;

  DoRequest(request, reply, err, &RemoteAPI::AddressSpace);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_EQ(18UL, reply.map.size());

  EXPECT_EQ("", reply.map[0].name);
  EXPECT_EQ(0x12766084a000UL, reply.map[0].base);
  EXPECT_EQ(262144UL, reply.map[0].size);
  EXPECT_EQ(0UL, reply.map[0].depth);

  EXPECT_EQ("", reply.map[1].name);
  EXPECT_EQ(0x1a531e112000UL, reply.map[1].base);
  EXPECT_EQ(262144UL, reply.map[1].size);
  EXPECT_EQ(0UL, reply.map[1].depth);

  EXPECT_EQ("", reply.map[2].name);
  EXPECT_EQ(0x38b28bf10000UL, reply.map[2].base);
  EXPECT_EQ(4096UL, reply.map[2].size);
  EXPECT_EQ(0UL, reply.map[2].depth);

  EXPECT_EQ("", reply.map[3].name);
  EXPECT_EQ(0x41ea65c3d000UL, reply.map[3].base);
  EXPECT_EQ(4096UL, reply.map[3].size);
  EXPECT_EQ(0UL, reply.map[3].depth);

  EXPECT_EQ("", reply.map[4].name);
  EXPECT_EQ(0x44b8c3369000UL, reply.map[4].base);
  EXPECT_EQ(2097152UL, reply.map[4].size);
  EXPECT_EQ(0UL, reply.map[4].depth);

  EXPECT_EQ("", reply.map[5].name);
  EXPECT_EQ(0x45226ca65000UL, reply.map[5].base);
  EXPECT_EQ(2097152UL, reply.map[5].size);
  EXPECT_EQ(0UL, reply.map[5].depth);

  EXPECT_EQ("", reply.map[6].name);
  EXPECT_EQ(0x513737c43000UL, reply.map[6].base);
  EXPECT_EQ(28672UL, reply.map[6].size);
  EXPECT_EQ(0UL, reply.map[6].depth);

  EXPECT_EQ("", reply.map[7].name);
  EXPECT_EQ(0x513737c4a000UL, reply.map[7].base);
  EXPECT_EQ(4096UL, reply.map[7].size);
  EXPECT_EQ(0UL, reply.map[7].depth);

  EXPECT_EQ("", reply.map[8].name);
  EXPECT_EQ(0x5e008a746000UL, reply.map[8].base);
  EXPECT_EQ(139264UL, reply.map[8].size);
  EXPECT_EQ(0UL, reply.map[8].depth);

  EXPECT_EQ("", reply.map[9].name);
  EXPECT_EQ(0x5e008a768000UL, reply.map[9].base);
  EXPECT_EQ(8192UL, reply.map[9].size);
  EXPECT_EQ(0UL, reply.map[9].depth);

  EXPECT_EQ("", reply.map[10].name);
  EXPECT_EQ(0x5e008a76a000UL, reply.map[10].base);
  EXPECT_EQ(12288UL, reply.map[10].size);
  EXPECT_EQ(0UL, reply.map[10].depth);

  EXPECT_EQ("", reply.map[11].name);
  EXPECT_EQ(0x652d9b6bb000UL, reply.map[11].base);
  EXPECT_EQ(831488UL, reply.map[11].size);
  EXPECT_EQ(0UL, reply.map[11].depth);

  EXPECT_EQ("", reply.map[12].name);
  EXPECT_EQ(0x652d9b787000UL, reply.map[12].base);
  EXPECT_EQ(12288UL, reply.map[12].size);
  EXPECT_EQ(0UL, reply.map[12].depth);

  EXPECT_EQ("", reply.map[13].name);
  EXPECT_EQ(0x652d9b78a000UL, reply.map[13].base);
  EXPECT_EQ(12288UL, reply.map[13].size);
  EXPECT_EQ(0UL, reply.map[13].depth);

  EXPECT_EQ("", reply.map[14].name);
  EXPECT_EQ(0x7328c9333000UL, reply.map[14].base);
  EXPECT_EQ(8192UL, reply.map[14].size);
  EXPECT_EQ(0UL, reply.map[14].depth);

  EXPECT_EQ("", reply.map[15].name);
  EXPECT_EQ(0x7328c9335000UL, reply.map[15].base);
  EXPECT_EQ(4096UL, reply.map[15].size);
  EXPECT_EQ(0UL, reply.map[15].depth);

  EXPECT_EQ("", reply.map[16].name);
  EXPECT_EQ(0x7328c9336000UL, reply.map[16].base);
  EXPECT_EQ(4096UL, reply.map[16].size);
  EXPECT_EQ(0UL, reply.map[16].depth);

  EXPECT_EQ("", reply.map[17].name);
  EXPECT_EQ(0x7c1d710c8000UL, reply.map[17].base);
  EXPECT_EQ(4096UL, reply.map[17].size);
  EXPECT_EQ(0UL, reply.map[17].depth);
}

TEST_F(MinidumpTest, ReadMemory) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  Err err;
  debug_ipc::ReadMemoryRequest request;
  debug_ipc::ReadMemoryReply reply;

  request.process_koid = kTestExampleMinidumpKOID;
  request.address = kTestExampleMinidumpStackAddr;
  request.size = kTestExampleMinidumpStackSize;

  DoRequest(request, reply, err, &RemoteAPI::ReadMemory);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_EQ(1u, reply.blocks.size());
  const auto& block = reply.blocks.back();

  EXPECT_EQ(kTestExampleMinidumpStackAddr, block.address);
  EXPECT_EQ(kTestExampleMinidumpStackSize, block.size);
  ASSERT_TRUE(block.valid);
  ASSERT_EQ(block.size, block.data.size());

  EXPECT_EQ(0u, block.data[0]);
  EXPECT_EQ(0u, block.data[10]);
  EXPECT_EQ(0u, block.data[100]);
  EXPECT_EQ(0u, block.data[1000]);
  EXPECT_EQ(0u, block.data[10000]);
  EXPECT_EQ(0u, block.data[100000]);

  EXPECT_EQ(2u, block.data[260400]);
  EXPECT_EQ(0u, block.data[260401]);
  EXPECT_EQ(0u, block.data[260402]);
  EXPECT_EQ(0u, block.data[260403]);
  EXPECT_EQ(0u, block.data[260404]);
  EXPECT_EQ(240u, block.data[260410]);
  EXPECT_EQ(251u, block.data[260420]);
  EXPECT_EQ(0u, block.data[260430]);
  EXPECT_EQ(1u, block.data[260440]);
}

}  // namespace zxdb
