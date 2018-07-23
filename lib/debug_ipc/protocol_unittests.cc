// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/agent_protocol.h"
#include "garnet/lib/debug_ipc/client_protocol.h"
#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/protocol_helpers.h"
#include "gtest/gtest.h"

namespace debug_ipc {

namespace {

template <typename RequestType>
bool SerializeDeserializeRequest(const RequestType& in, RequestType* out) {
  MessageWriter writer;
  uint32_t in_transaction_id = 32;
  WriteRequest(in, in_transaction_id, &writer);

  std::vector<char> serialized = writer.MessageComplete();

  MessageReader reader(std::move(serialized));
  uint32_t out_transaction_id = 0;
  if (!ReadRequest(&reader, out, &out_transaction_id))
    return false;
  EXPECT_EQ(in_transaction_id, out_transaction_id);
  return true;
}

template <typename ReplyType>
bool SerializeDeserializeReply(const ReplyType& in, ReplyType* out) {
  MessageWriter writer;
  uint32_t in_transaction_id = 32;
  WriteReply(in, in_transaction_id, &writer);

  std::vector<char> serialized = writer.MessageComplete();

  MessageReader reader(std::move(serialized));
  uint32_t out_transaction_id = 0;
  if (!ReadReply(&reader, out, &out_transaction_id))
    return false;
  EXPECT_EQ(in_transaction_id, out_transaction_id);
  return true;
}

template <typename NotificationType>
bool SerializeDeserializeNotification(
    const NotificationType& in, NotificationType* out,
    void (*write_fn)(const NotificationType&, MessageWriter*),
    bool (*read_fn)(MessageReader*, NotificationType*)) {
  MessageWriter writer;
  write_fn(in, &writer);

  MessageReader reader(writer.MessageComplete());
  return read_fn(&reader, out);
}

}  // namespace

// Hello -----------------------------------------------------------------------

TEST(Protocol, HelloRequest) {
  HelloRequest initial;
  HelloRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
}

TEST(Protocol, HelloReply) {
  HelloReply initial;
  initial.version = 12345678;
  HelloReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));
  EXPECT_EQ(initial.version, second.version);
}

// Launch ----------------------------------------------------------------------

TEST(Protocol, LaunchRequest) {
  LaunchRequest initial;
  initial.argv.push_back("/usr/bin/WINWORD.EXE");
  initial.argv.push_back("--dosmode");

  LaunchRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  ASSERT_EQ(initial.argv.size(), second.argv.size());
  for (size_t i = 0; i < initial.argv.size(); i++)
    EXPECT_EQ(initial.argv[i], second.argv[i]);
}

TEST(Protocol, LaunchReply) {
  LaunchReply initial;
  initial.status = 67;
  initial.process_koid = 0x1234;
  initial.process_name = "winword.exe";

  LaunchReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));
  EXPECT_EQ(initial.status, second.status);
  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.process_name, second.process_name);
}

// Kill ----------------------------------------------------------------------

TEST(Protocol, KillRequest) {
  KillRequest initial;
  initial.process_koid = 5678;

  KillRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.process_koid, second.process_koid);
}

TEST(Protocol, KillReply) {
  KillReply initial;
  initial.status = 67;

  KillReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));
  EXPECT_EQ(initial.status, second.status);
}

// Attach ----------------------------------------------------------------------

TEST(Protocol, AttachRequest) {
  AttachRequest initial;
  initial.koid = 5678;

  AttachRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.koid, second.koid);
}

TEST(Protocol, AttachReply) {
  AttachReply initial;
  initial.status = 67;
  initial.process_name = "virtual console";

  AttachReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));
  EXPECT_EQ(initial.status, second.status);
  EXPECT_EQ(initial.process_name, second.process_name);
}

// Detach ----------------------------------------------------------------------

TEST(Protocol, DetachRequest) {
  DetachRequest initial;
  initial.process_koid = 5678;

  DetachRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.process_koid, second.process_koid);
}

TEST(Protocol, DetachReply) {
  DetachReply initial;
  initial.status = 67;

  DetachReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));
  EXPECT_EQ(initial.status, second.status);
}

// Pause ---------------------------------------------------------------------

TEST(Protocol, PauseRequest) {
  PauseRequest initial;
  initial.process_koid = 3746234;
  initial.thread_koid = 123523;

  PauseRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.thread_koid, second.thread_koid);
}

// Resume --------------------------------------------------------------------

TEST(Protocol, ResumeRequest) {
  ResumeRequest initial;
  initial.process_koid = 3746234;
  initial.thread_koids.push_back(123523);
  initial.how = ResumeRequest::How::kStepInRange;
  initial.range_begin = 0x12345;
  initial.range_end = 0x123456;

  ResumeRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.thread_koids, second.thread_koids);
  EXPECT_EQ(initial.how, second.how);
  EXPECT_EQ(initial.range_begin, second.range_begin);
  EXPECT_EQ(initial.range_end, second.range_end);
}

// ProcessTree -----------------------------------------------------------------

TEST(Protocol, ProcessTreeRequest) {
  ProcessTreeRequest initial;
  ProcessTreeRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
}

TEST(Protocol, ProcessTreeReply) {
  ProcessTreeReply initial;
  initial.root.type = ProcessTreeRecord::Type::kJob;
  initial.root.koid = 1234;
  initial.root.name = "root";

  initial.root.children.resize(1);
  initial.root.children[0].type = ProcessTreeRecord::Type::kProcess;
  initial.root.children[0].koid = 3456;
  initial.root.children[0].name = "hello";

  ProcessTreeReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  EXPECT_EQ(initial.root.type, second.root.type);
  EXPECT_EQ(initial.root.koid, second.root.koid);
  EXPECT_EQ(initial.root.name, second.root.name);
  ASSERT_EQ(initial.root.children.size(), second.root.children.size());
  EXPECT_EQ(initial.root.children[0].type, second.root.children[0].type);
  EXPECT_EQ(initial.root.children[0].koid, second.root.children[0].koid);
  EXPECT_EQ(initial.root.children[0].name, second.root.children[0].name);
}

// Threads ---------------------------------------------------------------------

TEST(Protocol, ThreadsRequest) {
  ThreadsRequest initial;
  initial.process_koid = 36473476;

  ThreadsRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.process_koid, second.process_koid);
}

TEST(Protocol, ThreadsReply) {
  ThreadsReply initial;
  initial.threads.resize(2);
  initial.threads[0].koid = 1234;
  initial.threads[0].name = "one";
  initial.threads[1].koid = 7634;
  initial.threads[1].name = "two";

  ThreadsReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  ASSERT_EQ(initial.threads.size(), second.threads.size());
  EXPECT_EQ(initial.threads[0].koid, second.threads[0].koid);
  EXPECT_EQ(initial.threads[0].name, second.threads[0].name);
  EXPECT_EQ(initial.threads[1].koid, second.threads[1].koid);
  EXPECT_EQ(initial.threads[1].name, second.threads[1].name);
}

// ReadMemory ------------------------------------------------------------------

TEST(Protocol, ReadMemoryRequest) {
  ReadMemoryRequest initial;
  initial.process_koid = 91823765;
  initial.address = 983462384;
  initial.size = 93453926;

  ReadMemoryRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));
  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.address, second.address);
  EXPECT_EQ(initial.size, second.size);
}

TEST(Protocol, ReadMemoryReply) {
  ReadMemoryReply initial;
  initial.blocks.resize(2);
  initial.blocks[0].address = 876234;
  initial.blocks[0].valid = true;
  initial.blocks[0].size = 12;
  for (uint64_t i = 0; i < initial.blocks[0].size; i++)
    initial.blocks[0].data.push_back(static_cast<uint8_t>(i));

  initial.blocks[1].address = 89362454;
  initial.blocks[1].valid = false;
  initial.blocks[1].size = 0;

  ReadMemoryReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  ASSERT_EQ(initial.blocks.size(), second.blocks.size());

  EXPECT_EQ(initial.blocks[0].address, second.blocks[0].address);
  EXPECT_EQ(initial.blocks[0].valid, second.blocks[0].valid);
  EXPECT_EQ(initial.blocks[0].size, second.blocks[0].size);
  EXPECT_EQ(second.blocks[0].size, second.blocks[0].data.size());
  for (uint64_t i = 0; i < second.blocks[0].size; i++)
    EXPECT_EQ(static_cast<uint8_t>(i), second.blocks[0].data[i]);

  EXPECT_EQ(initial.blocks[1].address, second.blocks[1].address);
  EXPECT_EQ(initial.blocks[1].valid, second.blocks[1].valid);
  EXPECT_EQ(initial.blocks[1].size, second.blocks[1].size);
  EXPECT_TRUE(second.blocks[1].data.empty());
}

// AddOrChangeBreakpoint -------------------------------------------------------

TEST(Protocol, AddOrChangeBreakpointRequest) {
  AddOrChangeBreakpointRequest initial;
  initial.breakpoint.breakpoint_id = 8976;
  initial.breakpoint.stop = debug_ipc::Stop::kProcess;
  initial.breakpoint.locations.resize(1);

  ProcessBreakpointSettings& pr_settings = initial.breakpoint.locations.back();
  pr_settings.process_koid = 1234;
  pr_settings.thread_koid = 14612;
  pr_settings.address = 0x723456234;

  AddOrChangeBreakpointRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));

  EXPECT_EQ(initial.breakpoint.breakpoint_id, second.breakpoint.breakpoint_id);
  EXPECT_EQ(initial.breakpoint.stop, second.breakpoint.stop);
  ASSERT_EQ(initial.breakpoint.locations.size(),
            second.breakpoint.locations.size());

  EXPECT_EQ(initial.breakpoint.locations[0].process_koid,
            second.breakpoint.locations[0].process_koid);
  EXPECT_EQ(initial.breakpoint.locations[0].thread_koid,
            second.breakpoint.locations[0].thread_koid);
  EXPECT_EQ(initial.breakpoint.locations[0].address,
            second.breakpoint.locations[0].address);
}

TEST(Protocol, AddOrChangeBreakpointReply) {
  AddOrChangeBreakpointReply initial;
  initial.status = 78;

  AddOrChangeBreakpointReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  EXPECT_EQ(initial.status, second.status);
}

// RemoveBreakpoint ------------------------------------------------------------

TEST(Protocol, RemoveBreakpointRequest) {
  RemoveBreakpointRequest initial;
  initial.breakpoint_id = 8976;

  RemoveBreakpointRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));

  EXPECT_EQ(initial.breakpoint_id, second.breakpoint_id);
}

TEST(Protocol, RemoveBreakpointReply) {
  RemoveBreakpointReply initial;
  RemoveBreakpointReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));
}

// Backtrace -------------------------------------------------------------------

TEST(Protocol, BacktraceRequest) {
  BacktraceRequest initial;
  initial.process_koid = 1234;
  initial.thread_koid = 8976;

  BacktraceRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));

  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.thread_koid, second.thread_koid);
}

TEST(Protocol, BacktraceReply) {
  BacktraceReply initial;
  initial.frames.resize(2);
  initial.frames[0].ip = 1234;
  initial.frames[0].sp = 9875;
  initial.frames[1].ip = 71562341;
  initial.frames[1].sp = 89236413;

  BacktraceReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  EXPECT_EQ(2u, second.frames.size());
  EXPECT_EQ(initial.frames[0].ip, second.frames[0].ip);
  EXPECT_EQ(initial.frames[0].sp, second.frames[0].sp);
  EXPECT_EQ(initial.frames[1].ip, second.frames[1].ip);
  EXPECT_EQ(initial.frames[1].sp, second.frames[1].sp);
}

// Modules ---------------------------------------------------------------------

TEST(Protocol, ModulesRequest) {
  ModulesRequest initial;
  initial.process_koid = 1234;

  ModulesRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));

  EXPECT_EQ(initial.process_koid, second.process_koid);
}

TEST(Protocol, ModulesReply) {
  ModulesReply initial;
  initial.modules.resize(2);
  initial.modules[0].name = "winnt.dll";
  initial.modules[0].base = 0x1234567890;
  initial.modules[1].name = "libncurses.so.1.0.0";
  initial.modules[1].base = 0x1000;

  ModulesReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  EXPECT_EQ(2u, second.modules.size());
  EXPECT_EQ(initial.modules[0].name, second.modules[0].name);
  EXPECT_EQ(initial.modules[0].base, second.modules[0].base);
  EXPECT_EQ(initial.modules[1].name, second.modules[1].name);
  EXPECT_EQ(initial.modules[1].base, second.modules[1].base);
}

// ASpace ----------------------------------------------------------------------

TEST(Protocol, AspaceRequest) {
  AddressSpaceRequest initial;
  initial.process_koid = 1234;
  initial.address = 0x717171;

  AddressSpaceRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));

  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.address, second.address);
}

TEST(Protocol, AspaceReply) {
  const uint64_t kOneT = 1024 * 1024u * 1024u * 1024ull;
  AddressSpaceReply initial;

  initial.map.resize(4u);
  initial.map[0] = AddressRegion{"proc:5616", 0x1000000, 127 * kOneT, 0};
  initial.map[1] = AddressRegion{"root", 0x1000000, 127 * kOneT, 0};
  initial.map[2] = AddressRegion{"useralloc", 0x371f1276000, 12 * 1024, 1};
  initial.map[3] = AddressRegion{"initial-thread", 0x371f1277000, 4 * 1024, 2};

  AddressSpaceReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  ASSERT_EQ(4u, second.map.size());
  EXPECT_EQ(initial.map[0].name, second.map[0].name);
  EXPECT_EQ(initial.map[0].base, second.map[0].base);
  EXPECT_EQ(initial.map[0].size, second.map[0].size);
  EXPECT_EQ(initial.map[0].depth, second.map[0].depth);
  EXPECT_EQ(initial.map[1].name, second.map[1].name);
  EXPECT_EQ(initial.map[1].base, second.map[1].base);
  EXPECT_EQ(initial.map[1].size, second.map[1].size);
  EXPECT_EQ(initial.map[1].depth, second.map[1].depth);
  EXPECT_EQ(initial.map[2].name, second.map[2].name);
  EXPECT_EQ(initial.map[2].base, second.map[2].base);
  EXPECT_EQ(initial.map[2].size, second.map[2].size);
  EXPECT_EQ(initial.map[2].depth, second.map[2].depth);
  EXPECT_EQ(initial.map[3].name, second.map[3].name);
  EXPECT_EQ(initial.map[3].base, second.map[3].base);
  EXPECT_EQ(initial.map[3].size, second.map[3].size);
  EXPECT_EQ(initial.map[3].depth, second.map[3].depth);
}

// Registers -------------------------------------------------------------------

using debug_ipc::RegisterID;

std::vector<uint8_t> CreateData(size_t length) {
  std::vector<uint8_t> data;
  data.reserve(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = length;
  for (size_t i = 0; i < length; i++) {
    data.emplace_back(base - i);
  }
  return data;
}

debug_ipc::Register CreateRegister(RegisterID id,
                                   size_t length) {
  debug_ipc::Register reg;
  reg.id = id;
  reg.data = CreateData(length);
  return reg;
}

TEST(Protocol, RegistersRequest) {
  RegistersRequest initial;
  initial.process_koid = 0x1234;
  initial.thread_koid = 0x5678;

  RegistersRequest second;
  ASSERT_TRUE(SerializeDeserializeRequest(initial, &second));

  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.thread_koid, second.thread_koid);
}

TEST(Protocol, RegistersReply) {
  RegistersReply initial;

  RegisterCategory cat1;
  cat1.type = RegisterCategory::Type::kGeneral;
  cat1.registers.push_back(CreateRegister(RegisterID::kARMv8_lr, 1));
  cat1.registers.push_back(CreateRegister(RegisterID::kARMv8_pc, 2));
  cat1.registers.push_back(CreateRegister(RegisterID::kARMv8_sp, 4));
  cat1.registers.push_back(CreateRegister(RegisterID::kARMv8_cpsr, 8));
  initial.categories.push_back(cat1);

  // Sanity check
  ASSERT_EQ(*(uint8_t*)&(cat1.registers[0].data[0]), 0x01u);
  ASSERT_EQ(*(uint16_t*)&(cat1.registers[1].data[0]), 0x0102u);
  ASSERT_EQ(*(uint32_t*)&(cat1.registers[2].data[0]), 0x01020304u);
  ASSERT_EQ(*(uint64_t*)&(cat1.registers[3].data[0]), 0x0102030405060708u);

  RegisterCategory cat2;
  cat2.type = RegisterCategory::Type::kVector;
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x0, 1));
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x1, 2));
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x2, 4));
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x3, 8));
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x4, 16));
  initial.categories.push_back(cat2);

  RegistersReply second;
  ASSERT_TRUE(SerializeDeserializeReply(initial, &second));

  ASSERT_EQ(second.categories.size(), 2u);

  // Check cat1
  auto& out_cat1= second.categories[0];
  EXPECT_EQ(out_cat1.type, cat1.type);
  ASSERT_EQ(out_cat1.registers.size(), 4u);
  EXPECT_EQ(out_cat1.registers[0].id, cat1.registers[0].id);
  EXPECT_EQ(out_cat1.registers[0].data, cat1.registers[0].data);
  EXPECT_EQ(out_cat1.registers[1].id, cat1.registers[1].id);
  EXPECT_EQ(out_cat1.registers[1].data, cat1.registers[1].data);
  EXPECT_EQ(out_cat1.registers[2].id, cat1.registers[2].id);
  EXPECT_EQ(out_cat1.registers[2].data, cat1.registers[2].data);

  // Check cat2
  auto& out_cat2 = second.categories[1];
  EXPECT_EQ(out_cat2.type, cat2.type);
  ASSERT_EQ(out_cat2.registers.size(), 5u);
  EXPECT_EQ(out_cat2.registers[0].id, cat2.registers[0].id);
  EXPECT_EQ(out_cat2.registers[0].data, cat2.registers[0].data);
  EXPECT_EQ(out_cat2.registers[1].id, cat2.registers[1].id);
  EXPECT_EQ(out_cat2.registers[1].data, cat2.registers[1].data);
  EXPECT_EQ(out_cat2.registers[2].id, cat2.registers[2].id);
  EXPECT_EQ(out_cat2.registers[2].data, cat2.registers[2].data);
  EXPECT_EQ(out_cat2.registers[3].id, cat2.registers[3].id);
  EXPECT_EQ(out_cat2.registers[3].data, cat2.registers[3].data);
}

// Notifications ---------------------------------------------------------------

TEST(Protocol, NotifyThread) {
  NotifyThread initial;
  initial.process_koid = 9887;
  initial.record.koid = 1234;
  initial.record.name = "Wolfgang";
  initial.record.state = ThreadRecord::State::kDying;

  MessageWriter writer;
  WriteNotifyThread(MsgHeader::Type::kNotifyThreadStarting, initial, &writer);

  MessageReader reader(writer.MessageComplete());
  NotifyThread second;
  ASSERT_TRUE(ReadNotifyThread(&reader, &second));

  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.record.koid, second.record.koid);
  EXPECT_EQ(initial.record.name, second.record.name);
  EXPECT_EQ(initial.record.state, second.record.state);
}

TEST(Protocol, NotifyException) {
  NotifyException initial;
  initial.process_koid = 23;
  initial.thread.name = "foo";
  initial.type = NotifyException::Type::kHardware;
  initial.frame.ip = 0x7647342634;
  initial.frame.sp = 0x9861238251;

  initial.hit_breakpoints.emplace_back();
  initial.hit_breakpoints[0].breakpoint_id = 45;
  initial.hit_breakpoints[0].hit_count = 15;
  initial.hit_breakpoints[0].should_delete = true;

  initial.hit_breakpoints.emplace_back();
  initial.hit_breakpoints[1].breakpoint_id = 46;
  initial.hit_breakpoints[1].hit_count = 16;
  initial.hit_breakpoints[1].should_delete = false;

  NotifyException second;
  ASSERT_TRUE(SerializeDeserializeNotification(
      initial, &second, &WriteNotifyException, &ReadNotifyException));

  EXPECT_EQ(initial.process_koid, second.process_koid);
  EXPECT_EQ(initial.thread.name, second.thread.name);
  EXPECT_EQ(initial.type, second.type);
  EXPECT_EQ(initial.frame.ip, second.frame.ip);
  EXPECT_EQ(initial.frame.sp, second.frame.sp);
  ASSERT_EQ(initial.hit_breakpoints.size(), second.hit_breakpoints.size());

  EXPECT_EQ(initial.hit_breakpoints[0].breakpoint_id,
            second.hit_breakpoints[0].breakpoint_id);
  EXPECT_EQ(initial.hit_breakpoints[0].hit_count,
            second.hit_breakpoints[0].hit_count);
  EXPECT_EQ(initial.hit_breakpoints[0].should_delete,
            second.hit_breakpoints[0].should_delete);

  EXPECT_EQ(initial.hit_breakpoints[1].breakpoint_id,
            second.hit_breakpoints[1].breakpoint_id);
  EXPECT_EQ(initial.hit_breakpoints[1].hit_count,
            second.hit_breakpoints[1].hit_count);
  EXPECT_EQ(initial.hit_breakpoints[1].should_delete,
            second.hit_breakpoints[1].should_delete);
}

TEST(Protocol, NotifyModules) {
  NotifyModules initial;
  initial.process_koid = 23;
  initial.modules.resize(2);
  initial.modules[0].name = "foo";
  initial.modules[0].base = 0x12345;
  initial.modules[1].name = "bar";
  initial.modules[1].base = 0x43567;
  initial.stopped_thread_koids.push_back(34);
  initial.stopped_thread_koids.push_back(96);

  NotifyModules second;
  ASSERT_TRUE(SerializeDeserializeNotification(
      initial, &second, &WriteNotifyModules, &ReadNotifyModules));

  EXPECT_EQ(initial.process_koid, second.process_koid);
  ASSERT_EQ(initial.modules.size(), second.modules.size());
  EXPECT_EQ(initial.modules[0].name, second.modules[0].name);
  EXPECT_EQ(initial.modules[0].base, second.modules[0].base);
  EXPECT_EQ(initial.modules[1].name, second.modules[1].name);
  EXPECT_EQ(initial.modules[1].base, second.modules[1].base);
  EXPECT_EQ(initial.stopped_thread_koids, second.stopped_thread_koids);
}

}  // namespace debug_ipc
