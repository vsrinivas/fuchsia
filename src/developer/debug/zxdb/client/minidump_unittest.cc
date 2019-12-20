// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>
#include <map>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system_observer.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/developer/debug/zxdb/common/host_util.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"

namespace zxdb {

class MinidumpTest : public TestWithLoop, public ThreadObserver, public SystemObserver {
 public:
  MinidumpTest();
  virtual ~MinidumpTest();

  Session& session() { return *session_; }
  debug_ipc::ExceptionType last_hit() { return last_hit_; }

  Err TryOpen(const std::string& filename);

  template <typename RequestType, typename ReplyType>
  void DoRequest(RequestType request, ReplyType& reply, Err& err,
                 void (RemoteAPI::*handler)(const RequestType&,
                                            fit::callback<void(const Err&, ReplyType)>));

  // ThreadObserver implementation.
  void OnThreadStopped(Thread*, debug_ipc::ExceptionType type,
                       const std::vector<fxl::WeakPtr<Breakpoint>>&) override;

 private:
  debug_ipc::ExceptionType last_hit_ = debug_ipc::ExceptionType::kNone;
  std::unique_ptr<Session> session_;
};

MinidumpTest::MinidumpTest() {
  session_ = std::make_unique<Session>();
  session().thread_observers().AddObserver(this);
}

MinidumpTest::~MinidumpTest() { session().thread_observers().RemoveObserver(this); }

Err MinidumpTest::TryOpen(const std::string& filename) {
  static auto data_dir = std::filesystem::path(GetSelfPath()).parent_path() / "test_data" / "zxdb";

  Err err;
  auto path = (data_dir / filename).string();

  session().OpenMinidump(path, [&err](const Err& got) { err = got; });

  loop().RunUntilNoTasks();

  return err;
}

template <typename RequestType, typename ReplyType>
void MinidumpTest::DoRequest(
    RequestType request, ReplyType& reply, Err& err,
    void (RemoteAPI::*handler)(const RequestType&, fit::callback<void(const Err&, ReplyType)>)) {
  (session().remote_api()->*handler)(request, [&reply, &err](const Err& e, ReplyType r) {
    err = e;
    reply = r;
  });
  loop().RunUntilNoTasks();
}

void MinidumpTest::OnThreadStopped(Thread*, debug_ipc::ExceptionType type,
                                   const std::vector<fxl::WeakPtr<Breakpoint>>&) {
  last_hit_ = type;
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
constexpr uint32_t kTestExampleMinidumpNewCvRecordKOID = 12843UL;
constexpr uint32_t kTestExampleMinidumpThreadKOID = 671806UL;
constexpr uint64_t kTestExampleMinidumpStackAddr = 0x37f880947000;
constexpr uint32_t kTestExampleMinidumpStackSize = 0x40000;

constexpr uint32_t kTestExampleMinidumpWithAspaceKOID = 9462UL;

TEST_F(MinidumpTest, Load) {
  EXPECT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  EXPECT_NE(nullptr, session().system().ProcessFromKoid(kTestExampleMinidumpKOID));
}

TEST_F(MinidumpTest, ProcessTreeRecord) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  EXPECT_EQ(debug_ipc::ExceptionType::kUndefinedInstruction, last_hit());

  Err err;
  debug_ipc::ProcessTreeReply reply;
  DoRequest(debug_ipc::ProcessTreeRequest(), reply, err, &RemoteAPI::ProcessTree);
  ASSERT_ZXDB_SUCCESS(err);

  auto record = reply.root;
  EXPECT_EQ(debug_ipc::ProcessTreeRecord::Type::kProcess, record.type);
  EXPECT_EQ("scenic", record.name);
  EXPECT_EQ(kTestExampleMinidumpKOID, record.koid);
  EXPECT_EQ(0UL, record.children.size());
}

TEST_F(MinidumpTest, AttachDetach) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  EXPECT_EQ(debug_ipc::ExceptionType::kUndefinedInstruction, last_hit());

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

  EXPECT_EQ(debug_ipc::ExceptionType::kUndefinedInstruction, last_hit());

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

  EXPECT_EQ(debug_ipc::ExceptionType::kUndefinedInstruction, last_hit());

  Err err;
  debug_ipc::ThreadsRequest request;
  debug_ipc::ThreadsReply reply;

  request.process_koid = kTestExampleMinidumpKOID;
  DoRequest(request, reply, err, &RemoteAPI::Threads);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_LT(0UL, reply.threads.size());
  EXPECT_EQ(1UL, reply.threads.size());

  auto& thread = reply.threads[0];

  EXPECT_EQ(kTestExampleMinidumpThreadKOID, thread.thread_koid);
  EXPECT_EQ("", thread.name);
  EXPECT_EQ(debug_ipc::ThreadRecord::State::kCoreDump, thread.state);
}

TEST_F(MinidumpTest, Registers) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  EXPECT_EQ(debug_ipc::ExceptionType::kUndefinedInstruction, last_hit());

  Err err;
  debug_ipc::ReadRegistersRequest request;
  debug_ipc::ReadRegistersReply reply;

  using C = debug_ipc::RegisterCategory;
  using R = debug_ipc::RegisterID;

  request.process_koid = kTestExampleMinidumpKOID;
  request.thread_koid = kTestExampleMinidumpThreadKOID;
  request.categories = {
      C::kGeneral,
      C::kFloatingPoint,
      C::kVector,
      C::kDebug,
  };
  DoRequest(request, reply, err, &RemoteAPI::ReadRegisters);
  ASSERT_ZXDB_SUCCESS(err);

  std::map<R, std::vector<uint8_t>> got;
  for (const auto& reg : reply.registers)
    got[reg.id] = reg.data;

  std::vector<uint8_t> zero_short = {0, 0};
  std::vector<uint8_t> zero_128 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  EXPECT_EQ(AsData(0x83UL), got[R::kX64_rax]);
  EXPECT_EQ(AsData(0x2FE150062100UL), got[R::kX64_rbx]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_rcx]);
  EXPECT_EQ(AsData(0x4DC647A67264UL), got[R::kX64_rdx]);
  EXPECT_EQ(AsData(0x5283B9A79945UL), got[R::kX64_rsi]);
  EXPECT_EQ(AsData(0x4DC647A671D8UL), got[R::kX64_rdi]);
  EXPECT_EQ(AsData(0x37F880986D70UL), got[R::kX64_rbp]);
  EXPECT_EQ(AsData(0x37F880986D48UL), got[R::kX64_rsp]);
  EXPECT_EQ(AsData(0x1UL), got[R::kX64_r8]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_r9]);
  EXPECT_EQ(AsData(0x4DC647A671D8UL), got[R::kX64_r10]);
  EXPECT_EQ(AsData(0x83UL), got[R::kX64_r11]);
  EXPECT_EQ(AsData(0x2FE150077070UL), got[R::kX64_r12]);
  EXPECT_EQ(AsData(0x3F4C20970A28UL), got[R::kX64_r13]);
  EXPECT_EQ(AsData(0xFFFFFFF5UL), got[R::kX64_r14]);
  EXPECT_EQ(AsData(0x2FE150062138UL), got[R::kX64_r15]);
  EXPECT_EQ(AsData(0x4DC6479A5B1EUL), got[R::kX64_rip]);
  EXPECT_EQ(AsData(0x10206UL), got[R::kX64_rflags]);

  EXPECT_EQ(zero_short, got[R::kX64_fcw]);
  EXPECT_EQ(zero_short, got[R::kX64_fsw]);
  EXPECT_EQ(AsData('\0'), got[R::kX64_ftw]);
  EXPECT_EQ(zero_short, got[R::kX64_fop]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_fip]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_fdp]);
  EXPECT_EQ(zero_128, got[R::kX64_st0]);
  EXPECT_EQ(zero_128, got[R::kX64_st1]);
  EXPECT_EQ(zero_128, got[R::kX64_st2]);
  EXPECT_EQ(zero_128, got[R::kX64_st3]);
  EXPECT_EQ(zero_128, got[R::kX64_st4]);
  EXPECT_EQ(zero_128, got[R::kX64_st5]);
  EXPECT_EQ(zero_128, got[R::kX64_st6]);
  EXPECT_EQ(zero_128, got[R::kX64_st7]);

  EXPECT_EQ(AsData(0x0U), got[R::kX64_mxcsr]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm0]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm1]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm2]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm3]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm4]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm5]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm6]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm7]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm8]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm9]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm10]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm11]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm12]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm13]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm14]);
  EXPECT_EQ(zero_128, got[R::kX64_xmm15]);

  EXPECT_EQ(AsData(0x0UL), got[R::kX64_dr0]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_dr1]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_dr2]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_dr3]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_dr6]);
  EXPECT_EQ(AsData(0x0UL), got[R::kX64_dr7]);
}

TEST_F(MinidumpTest, Modules) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump_new_cvrecord.dmp"));

  EXPECT_EQ(debug_ipc::ExceptionType::kSoftware, last_hit());

  Err err;
  debug_ipc::ModulesRequest request;
  debug_ipc::ModulesReply reply;

  request.process_koid = kTestExampleMinidumpNewCvRecordKOID;

  DoRequest(request, reply, err, &RemoteAPI::Modules);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_EQ(11UL, reply.modules.size());

  EXPECT_EQ("<_>", reply.modules[0].name);
  EXPECT_EQ(0xdb9f3c9ee000UL, reply.modules[0].base);
  EXPECT_EQ("bbe04258f9aee727", reply.modules[0].build_id);

  EXPECT_EQ("libfxl_logging.so", reply.modules[1].name);
  EXPECT_EQ(0x88fde9aa2000UL, reply.modules[1].base);
  EXPECT_EQ("6990f44a2b829d04", reply.modules[1].build_id);

  EXPECT_EQ("libfdio.so", reply.modules[2].name);
  EXPECT_EQ(0xf84d6c82a000UL, reply.modules[2].base);
  EXPECT_EQ("47521571b0824b71ddc745a01d7a0352539dd803", reply.modules[2].build_id);

  EXPECT_EQ("libzircon.so", reply.modules[3].name);
  EXPECT_EQ(0xe0a9f4b35000UL, reply.modules[3].base);
  EXPECT_EQ("b0cb33d5e533ba8f6dcb73cc9c158cb8247f0263", reply.modules[3].build_id);

  EXPECT_EQ("libasync-default.so", reply.modules[4].name);
  EXPECT_EQ(0xacc33bf02000UL, reply.modules[4].base);
  EXPECT_EQ("94dee2c0e27202b524255e07f7a9a9e5e282bdb0", reply.modules[4].build_id);

  EXPECT_EQ("libsyslog.so", reply.modules[5].name);
  EXPECT_EQ(0xf4e730afa000UL, reply.modules[5].base);
  EXPECT_EQ("d9ea935594739f99127a67a1816b4afa2d2fd486", reply.modules[5].build_id);

  EXPECT_EQ("libtrace-engine.so", reply.modules[6].name);
  EXPECT_EQ(0xe0f0f0035000UL, reply.modules[6].base);
  EXPECT_EQ("b1f55f8a9a49d4bd5040c17b69b3e795f5e9ee84", reply.modules[6].build_id);

  EXPECT_EQ("libc++.so.2", reply.modules[7].name);
  EXPECT_EQ(0xd9512a2b0000UL, reply.modules[7].base);
  EXPECT_EQ("e2805c6c256fe3bc", reply.modules[7].build_id);

  EXPECT_EQ("libc.so", reply.modules[8].name);
  EXPECT_EQ(0xd339f6596000UL, reply.modules[8].base);
  EXPECT_EQ("c92393053718b514a70777d18c4c0cc415d544b0", reply.modules[8].build_id);

  EXPECT_EQ("libc++abi.so.1", reply.modules[9].name);
  EXPECT_EQ(0xbcd34b71000UL, reply.modules[9].base);
  EXPECT_EQ("91766972c93894f3", reply.modules[9].build_id);

  EXPECT_EQ("libunwind.so.1", reply.modules[10].name);
  EXPECT_EQ(0xbcc263255000UL, reply.modules[10].base);
  EXPECT_EQ("3a4ebe2ee4046112", reply.modules[10].build_id);
}

TEST_F(MinidumpTest, AddressSpace) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump_with_aspace.dmp"));

  EXPECT_EQ(debug_ipc::ExceptionType::kPageFault, last_hit());

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

  EXPECT_EQ(debug_ipc::ExceptionType::kUndefinedInstruction, last_hit());

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

TEST_F(MinidumpTest, ReadMemory_Short) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  EXPECT_EQ(debug_ipc::ExceptionType::kUndefinedInstruction, last_hit());

  const uint32_t kOverReadSize = kTestExampleMinidumpStackSize + 36;

  Err err;
  debug_ipc::ReadMemoryRequest request;
  debug_ipc::ReadMemoryReply reply;

  request.process_koid = kTestExampleMinidumpKOID;
  request.address = kTestExampleMinidumpStackAddr;
  request.size = kOverReadSize;

  DoRequest(request, reply, err, &RemoteAPI::ReadMemory);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_EQ(2u, reply.blocks.size());
  const auto& block = reply.blocks.front();

  EXPECT_EQ(kTestExampleMinidumpStackAddr, block.address);
  EXPECT_EQ(kTestExampleMinidumpStackSize, block.size);
  ASSERT_TRUE(block.valid);
  ASSERT_EQ(block.size, block.data.size());

  const auto& bad_block = reply.blocks.back();

  EXPECT_EQ(kTestExampleMinidumpStackAddr + kTestExampleMinidumpStackSize, bad_block.address);
  EXPECT_EQ(kOverReadSize - kTestExampleMinidumpStackSize, bad_block.size);
  ASSERT_FALSE(bad_block.valid);
  ASSERT_EQ(0u, bad_block.data.size());
}

TEST_F(MinidumpTest, SysInfo) {
  ASSERT_ZXDB_SUCCESS(TryOpen("test_example_minidump.dmp"));

  EXPECT_EQ(debug_ipc::ExceptionType::kUndefinedInstruction, last_hit());

  Err err;
  debug_ipc::SysInfoRequest request;
  debug_ipc::SysInfoReply reply;

  DoRequest(request, reply, err, &RemoteAPI::SysInfo);
  ASSERT_ZXDB_SUCCESS(err);

  EXPECT_EQ("Zircon prerelease git-50fbb1100548dc716d72abd4024461a85f5c8eb8 x86_64", reply.version);
  EXPECT_EQ(4u, reply.num_cpus);
  EXPECT_EQ(0u, reply.memory_mb);
  EXPECT_EQ(0u, reply.hw_breakpoint_count);
  EXPECT_EQ(0u, reply.hw_watchpoint_count);
}

TEST_F(MinidumpTest, Backtrace) {
  const uint64_t kProcessKOID = 10363;
  const uint64_t kThreadKOID = 65232;
  auto core_dir = std::filesystem::path(GetSelfPath()).parent_path() / "test_data" / "zxdb" /
                  "sample_core" / "core";
  session().system().settings().SetList(ClientSettings::System::kSymbolPaths, {core_dir});

  ASSERT_ZXDB_SUCCESS(TryOpen(core_dir / "core.dmp"));

  EXPECT_EQ(debug_ipc::ExceptionType::kGeneral, last_hit());

  Err err;
  debug_ipc::ThreadStatusRequest request;
  debug_ipc::ThreadStatusReply reply;

  request.process_koid = kProcessKOID;
  request.thread_koid = kThreadKOID;
  DoRequest(request, reply, err, &RemoteAPI::ThreadStatus);
  ASSERT_ZXDB_SUCCESS(err);

  ASSERT_EQ(3u, reply.record.frames.size());
  EXPECT_EQ(0x6df7cb8a10a3u, reply.record.frames[0].ip);
  EXPECT_EQ(0x6df7cb8a1062u, reply.record.frames[1].ip);
  EXPECT_EQ(0x575953094967u, reply.record.frames[2].ip);
}

}  // namespace zxdb
