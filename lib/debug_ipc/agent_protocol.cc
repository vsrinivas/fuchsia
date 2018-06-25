// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/agent_protocol.h"

#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/protocol_helpers.h"

namespace debug_ipc {

// Record deserializers --------------------------------------------------------

bool Deserialize(MessageReader* reader, ProcessBreakpointSettings* settings) {
  if (!reader->ReadUint64(&settings->process_koid))
    return false;
  if (!reader->ReadUint64(&settings->thread_koid))
    return false;
  return reader->ReadUint64(&settings->address);
}

bool Deserialize(MessageReader* reader, BreakpointSettings* settings) {
  if (!reader->ReadUint32(&settings->breakpoint_id))
    return false;
  if (!reader->ReadBool(&settings->one_shot))
    return false;

  uint32_t stop;
  if (!reader->ReadUint32(&stop))
    return false;
  settings->stop = static_cast<Stop>(stop);

  return Deserialize(reader, &settings->locations);
}

// Record serializers ----------------------------------------------------------

void Serialize(const ProcessTreeRecord& record, MessageWriter* writer) {
  writer->WriteUint32(static_cast<uint32_t>(record.type));
  writer->WriteUint64(record.koid);
  writer->WriteString(record.name);
  Serialize(record.children, writer);
}

void Serialize(const ThreadRecord& record, MessageWriter* writer) {
  writer->WriteUint64(record.koid);
  writer->WriteString(record.name);
  writer->WriteUint32(static_cast<uint32_t>(record.state));
}

void Serialize(const MemoryBlock& block, MessageWriter* writer) {
  writer->WriteUint64(block.address);
  writer->WriteBool(block.valid);
  writer->WriteUint32(block.size);
  if (block.valid && block.size > 0)
    writer->WriteBytes(&block.data[0], block.size);
}

void Serialize(const Module& module, MessageWriter* writer) {
  writer->WriteString(module.name);
  writer->WriteUint64(module.base);
  writer->WriteString(module.build_id);
}

void Serialize(const Register& reg, MessageWriter* writer) {
  writer->WriteString(reg.name);
  writer->WriteUint64(reg.value);
}

void Serialize(const StackFrame& frame, MessageWriter* writer) {
  writer->WriteBytes(&frame, sizeof(StackFrame));
}

void Serialize(const AddressRegion& region, MessageWriter* writer) {
  writer->WriteString(region.name);
  writer->WriteUint64(region.base);
  writer->WriteUint64(region.size);
  writer->WriteUint64(region.depth);
}

void Serialize(const BreakpointStats& stats, MessageWriter* writer) {
  writer->WriteUint32(stats.breakpoint_id);
  writer->WriteUint32(stats.hit_count);
  writer->WriteBool(stats.should_delete);
}

// Hello -----------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, HelloRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

void WriteReply(const HelloReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kHello, transaction_id);
  writer->WriteBytes(&reply, sizeof(HelloReply));
}

// Launch ----------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, LaunchRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return Deserialize(reader, &request->argv);
}

void WriteReply(const LaunchReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kLaunch, transaction_id);
  writer->WriteUint32(reply.status);
  writer->WriteUint64(reply.process_koid);
  writer->WriteString(reply.process_name);
}

// Kill ----------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, KillRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadUint64(&request->process_koid);
}

void WriteReply(const KillReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kKill, transaction_id);
  writer->WriteUint32(reply.status);
}

// Attach ----------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, AttachRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadUint64(&request->koid);
}

void WriteReply(const AttachReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAttach, transaction_id);
  writer->WriteUint32(reply.status);
  writer->WriteString(reply.process_name);
}

// Detach ----------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, DetachRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadUint64(&request->process_koid);
}

void WriteReply(const DetachReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kDetach, transaction_id);
  writer->WriteUint32(reply.status);
}

// Pause -----------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, PauseRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  if (!reader->ReadUint64(&request->process_koid))
    return false;
  if (!reader->ReadUint64(&request->thread_koid))
    return false;
  return true;
}

void WriteReply(const PauseReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kPause, transaction_id);
}

// Resume ----------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ResumeRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  if (!reader->ReadUint64(&request->process_koid))
    return false;
  if (!reader->ReadUint64(&request->thread_koid))
    return false;

  uint32_t how;
  if (!reader->ReadUint32(&how))
    return false;
  if (how >= static_cast<uint32_t>(ResumeRequest::How::kLast))
    return false;
  request->how = static_cast<ResumeRequest::How>(how);

  if (!reader->ReadUint64(&request->range_begin))
    return false;
  if (!reader->ReadUint64(&request->range_end))
    return false;

  return true;
}

void WriteReply(const ResumeReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kResume, transaction_id);
}

// ProcessTree -----------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ProcessTreeRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ProcessTreeRequest), request);
}

void WriteReply(const ProcessTreeReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kProcessTree, transaction_id);
  Serialize(reply.root, writer);
}

// Threads ---------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ThreadsRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ThreadsRequest), request);
}

void WriteReply(const ThreadsReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kThreads, transaction_id);
  Serialize(reply.threads, writer);
}

// ReadMemory ------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ReadMemoryRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ReadMemoryRequest), request);
}

void WriteReply(const ReadMemoryReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kReadMemory, transaction_id);
  Serialize(reply.blocks, writer);
}

// AddOrChangeBreakpoint -------------------------------------------------------

bool ReadRequest(MessageReader* reader, AddOrChangeBreakpointRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return Deserialize(reader, &request->breakpoint);
}

void WriteReply(const AddOrChangeBreakpointReply& reply,
                uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAddOrChangeBreakpoint, transaction_id);
  writer->WriteUint32(reply.status);
}

// RemoveBreakpoint ------------------------------------------------------------

bool ReadRequest(MessageReader* reader, RemoveBreakpointRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(RemoveBreakpointRequest), request);
}

void WriteReply(const RemoveBreakpointReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kRemoveBreakpoint, transaction_id);
}

// Backtrace -------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, BacktraceRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(BacktraceRequest), request);
}

void WriteReply(const BacktraceReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kBacktrace, transaction_id);
  Serialize(reply.frames, writer);
}

// Modules ---------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ModulesRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ModulesRequest), request);
}

void WriteReply(const ModulesReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kModules, transaction_id);
  Serialize(reply.modules, writer);
}

// Registers ---------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, RegistersRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(RegistersRequest), request);
}

void WriteReply(const RegistersReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kRegisters, transaction_id);
  Serialize(reply.registers, writer);
}

// Address space ---------------------------------------------------------------

bool ReadRequest(MessageReader* reader, AddressSpaceRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(AddressSpaceRequest), request);
}

void WriteReply(const AddressSpaceReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAddressSpace, transaction_id);
  Serialize(reply.map, writer);
}

// Notifications ---------------------------------------------------------------

void WriteNotifyProcess(const NotifyProcess& notify, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kNotifyProcessExiting, 0);
  writer->WriteUint64(notify.process_koid);
  writer->WriteInt64(notify.return_code);
}

void WriteNotifyThread(MsgHeader::Type type, const NotifyThread& notify,
                       MessageWriter* writer) {
  writer->WriteHeader(type, 0);
  writer->WriteUint64(notify.process_koid);
  Serialize(notify.record, writer);
}

void WriteNotifyException(const NotifyException& notify,
                          MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kNotifyException, 0);
  writer->WriteUint64(notify.process_koid);
  Serialize(notify.thread, writer);
  writer->WriteUint32(static_cast<uint32_t>(notify.type));
  Serialize(notify.frame, writer);
  Serialize(notify.hit_breakpoints, writer);
}

}  // namespace debug_ipc
