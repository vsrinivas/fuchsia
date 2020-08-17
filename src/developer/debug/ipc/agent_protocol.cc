// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/agent_protocol.h"

#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/ipc/protocol_helpers.h"

namespace debug_ipc {

// Record deserializers ----------------------------------------------------------------------------

bool Deserialize(MessageReader* reader, ProcessBreakpointSettings* settings) {
  if (!reader->ReadUint64(&settings->process_koid) || !reader->ReadUint64(&settings->thread_koid) ||
      !reader->ReadUint64(&settings->address)) {
    return false;
  }
  return Deserialize(reader, &settings->address_range);
}

bool Deserialize(MessageReader* reader, BreakpointSettings* settings) {
  if (!reader->ReadUint32(&settings->id))
    return false;

  uint32_t type;
  if (!reader->ReadUint32(&type) || type >= static_cast<uint32_t>(BreakpointType::kLast))
    return false;
  settings->type = static_cast<BreakpointType>(type);

  if (!reader->ReadString(&settings->name))
    return false;
  if (!reader->ReadBool(&settings->one_shot))
    return false;

  uint32_t stop;
  if (!reader->ReadUint32(&stop))
    return false;
  settings->stop = static_cast<Stop>(stop);

  return Deserialize(reader, &settings->locations);
}

bool Deserialize(MessageReader* reader, ConfigAction* action) {
  uint32_t type = 0;
  if (!reader->ReadUint32(&type) || type >= static_cast<uint32_t>(ConfigAction::Type::kLast)) {
    return false;
  }
  action->type = static_cast<ConfigAction::Type>(type);
  if (!reader->ReadString(&action->value))
    return false;
  return true;
}

// Record serializers ------------------------------------------------------------------------------

void Serialize(const ProcessTreeRecord& record, MessageWriter* writer) {
  writer->WriteUint32(static_cast<uint32_t>(record.type));
  writer->WriteUint64(record.koid);
  writer->WriteString(record.name);
  Serialize(record.children, writer);
}

void Serialize(const ThreadRecord& record, MessageWriter* writer) {
  writer->WriteUint64(record.process_koid);
  writer->WriteUint64(record.thread_koid);
  writer->WriteString(record.name);
  writer->WriteUint32(static_cast<uint32_t>(record.state));
  writer->WriteUint32(static_cast<uint32_t>(record.blocked_reason));
  writer->WriteUint32(static_cast<uint32_t>(record.stack_amount));
  Serialize(record.frames, writer);
}

void Serialize(const ProcessRecord& record, MessageWriter* writer) {
  writer->WriteUint64(record.process_koid);
  writer->WriteString(record.process_name);
  Serialize(record.threads, writer);
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
  writer->WriteUint64(module.debug_address);
  writer->WriteString(module.build_id);
}

void Serialize(const StackFrame& frame, MessageWriter* writer) {
  writer->WriteUint64(frame.ip);
  writer->WriteUint64(frame.sp);
  writer->WriteUint64(frame.cfa);
  Serialize(frame.regs, writer);
}

void Serialize(const AddressRegion& region, MessageWriter* writer) {
  writer->WriteString(region.name);
  writer->WriteUint64(region.base);
  writer->WriteUint64(region.size);
  writer->WriteUint64(region.depth);
}

void Serialize(const BreakpointStats& stats, MessageWriter* writer) {
  writer->WriteUint32(stats.id);
  writer->WriteUint32(stats.hit_count);
  writer->WriteBool(stats.should_delete);
}

// Hello -------------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, HelloRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

void WriteReply(const HelloReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kHello, transaction_id);
  writer->WriteBytes(&reply, sizeof(HelloReply));
}

// Status ------------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, StatusRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

void WriteReply(const StatusReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kStatus, transaction_id);
  Serialize(reply.processes, writer);
  Serialize(reply.limbo, writer);
}

// ProcessStatus -----------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ProcessStatusRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  if (!reader->ReadUint64(&request->process_koid))
    return false;
  return true;
}

void WriteReply(const ProcessStatusReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kProcessStatus, transaction_id);
  writer->WriteUint32(reply.status);
}

// Launch ------------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, LaunchRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  uint32_t inferior_type;
  if (!reader->ReadUint32(&inferior_type) ||
      inferior_type >= static_cast<uint32_t>(InferiorType::kLast)) {
    return false;
  }

  request->inferior_type = static_cast<InferiorType>(inferior_type);
  return Deserialize(reader, &request->argv);
}

void WriteReply(const LaunchReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kLaunch, transaction_id);
  writer->WriteUint32(static_cast<uint32_t>(reply.inferior_type));
  writer->WriteUint32(reply.status);
  writer->WriteUint64(reply.process_id);
  writer->WriteUint64(reply.component_id);
  writer->WriteString(reply.process_name);
}

// Kill ------------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, KillRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadUint64(&request->process_koid);
}

void WriteReply(const KillReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kKill, transaction_id);
  writer->WriteUint32(reply.status);
}

// Attach ------------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, AttachRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  uint32_t type;
  if (!reader->ReadUint32(&type))
    return false;
  if (type >= static_cast<uint32_t>(TaskType::kLast))
    return false;
  request->type = static_cast<TaskType>(type);
  return reader->ReadUint64(&request->koid);
}

void WriteReply(const AttachReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAttach, transaction_id);
  writer->WriteUint64(reply.koid);
  writer->WriteUint32(reply.status);
  writer->WriteString(reply.name);
}

// Detach ------------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, DetachRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  uint32_t type;
  if (!reader->ReadUint32(&type))
    return false;
  if (type >= static_cast<uint32_t>(TaskType::kLast))
    return false;
  request->type = static_cast<TaskType>(type);
  return reader->ReadUint64(&request->koid);
}

void WriteReply(const DetachReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kDetach, transaction_id);
  writer->WriteUint32(reply.status);
}

// Pause -------------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, PauseRequest* request, uint32_t* transaction_id) {
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

void WriteReply(const PauseReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kPause, transaction_id);
  Serialize(reply.threads, writer);
}

// QuitAgent ---------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, QuitAgentRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

void WriteReply(const QuitAgentReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kQuitAgent, transaction_id);
}

// Resume ------------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ResumeRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  if (!reader->ReadUint64(&request->process_koid))
    return false;
  if (!Deserialize(reader, &request->thread_koids))
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

void WriteReply(const ResumeReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kResume, transaction_id);
}

// ProcessTree -------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ProcessTreeRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ProcessTreeRequest), request);
}

void WriteReply(const ProcessTreeReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kProcessTree, transaction_id);
  Serialize(reply.root, writer);
}

// Threads -----------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ThreadsRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ThreadsRequest), request);
}

void WriteReply(const ThreadsReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kThreads, transaction_id);
  Serialize(reply.threads, writer);
}

// ReadMemory --------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ReadMemoryRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ReadMemoryRequest), request);
}

void WriteReply(const ReadMemoryReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kReadMemory, transaction_id);
  Serialize(reply.blocks, writer);
}

// AddOrChangeBreakpoint ---------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, AddOrChangeBreakpointRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return Deserialize(reader, &request->breakpoint);
}

void WriteReply(const AddOrChangeBreakpointReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAddOrChangeBreakpoint, transaction_id);
  writer->WriteUint32(reply.status);
}

// RemoveBreakpoint --------------------------------------------------------------------------------

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

// SysInfo -----------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, SysInfoRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

void WriteReply(const SysInfoReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kSysInfo, transaction_id);
  writer->WriteString(reply.version);
  writer->WriteUint32(reply.num_cpus);
  writer->WriteUint32(reply.memory_mb);
  writer->WriteUint32(reply.hw_breakpoint_count);
  writer->WriteUint32(reply.hw_watchpoint_count);
}

// ThreadStatus ------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ThreadStatusRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ThreadStatusRequest), request);
}

void WriteReply(const ThreadStatusReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kThreadStatus, transaction_id);
  Serialize(reply.record, writer);
}

// Modules -----------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ModulesRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ModulesRequest), request);
}

void WriteReply(const ModulesReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kModules, transaction_id);
  Serialize(reply.modules, writer);
}

// JobFilter --------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, JobFilterRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  if (!reader->ReadUint64(&request->job_koid))
    return false;
  return Deserialize(reader, &request->filters);
}

void WriteReply(const JobFilterReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kJobFilter, transaction_id);
  writer->WriteUint32(reply.status);
  Serialize(reply.matched_processes, writer);
}

// WriteMemory -------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, WriteMemoryRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  if (!reader->ReadUint64(&request->process_koid))
    return false;
  if (!reader->ReadUint64(&request->address))
    return false;
  return Deserialize(reader, &request->data);
}

void WriteReply(const WriteMemoryReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kWriteMemory, transaction_id);
  writer->WriteUint64(reply.status);
}

// LoadInfoHandleTable
// -------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, LoadInfoHandleTableRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadUint64(&request->process_koid);
}

void WriteReply(const LoadInfoHandleTableReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kLoadInfoHandleTable, transaction_id);
  writer->WriteUint32(reply.status);
  Serialize(reply.handles, writer);
}

// UpdateGlobalSettings ---------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, UpdateGlobalSettingsRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return Deserialize(reader, &request->exception_strategies);
}

void WriteReply(const UpdateGlobalSettingsReply& reply, uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kUpdateGlobalSettings, transaction_id);
  writer->WriteUint32(reply.status);
}

// ReadRegisters -----------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ReadRegistersRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  if (!reader->ReadUint64(&request->process_koid) || !reader->ReadUint64(&request->thread_koid))
    return false;

  return Deserialize(reader, &request->categories);
}

void WriteReply(const ReadRegistersReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kReadRegisters, transaction_id);
  Serialize(reply.registers, writer);
}

// WriteRegisters ----------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, WriteRegistersRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  if (!reader->ReadUint64(&request->process_koid) || !reader->ReadUint64(&request->thread_koid))
    return false;
  return Deserialize(reader, &request->registers);
}

void WriteReply(const WriteRegistersReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kWriteRegisters, transaction_id);
  writer->WriteUint32(reply.status);
  Serialize(reply.registers, writer);
}

// Address space -----------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, AddressSpaceRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(AddressSpaceRequest), request);
}

void WriteReply(const AddressSpaceReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAddressSpace, transaction_id);
  Serialize(reply.map, writer);
}

// ConfigAgent -------------------------------------------------------------------------------------

bool ReadRequest(MessageReader* reader, ConfigAgentRequest* request, uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return Deserialize(reader, &request->actions);
}

void WriteReply(const ConfigAgentReply& reply, uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kConfigAgent, transaction_id);
  Serialize(reply.results, writer);
}

// Notifications -----------------------------------------------------------------------------------

void WriteNotifyProcessExiting(const NotifyProcessExiting& notify, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kNotifyProcessExiting, 0);
  writer->WriteUint64(notify.process_koid);
  writer->WriteInt64(notify.return_code);
}

void WriteNotifyProcessStarting(const NotifyProcessStarting& notify, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kNotifyProcessStarting, 0);
  writer->WriteUint32(static_cast<uint32_t>(notify.type));
  writer->WriteUint64(notify.koid);
  writer->WriteUint32(notify.component_id);
  writer->WriteString(notify.name);
}

void WriteNotifyThread(MsgHeader::Type type, const NotifyThread& notify, MessageWriter* writer) {
  writer->WriteHeader(type, 0);
  Serialize(notify.record, writer);
}

void WriteNotifyException(const NotifyException& notify, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kNotifyException, 0);
  Serialize(notify.thread, writer);
  writer->WriteUint32(static_cast<uint32_t>(notify.type));
  writer->WriteBytes(&notify.exception, sizeof(notify.exception));
  Serialize(notify.hit_breakpoints, writer);
  Serialize(notify.other_affected_threads, writer);
}

void WriteNotifyModules(const NotifyModules& notify, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kNotifyModules, 0);
  writer->WriteUint64(notify.process_koid);
  Serialize(notify.modules, writer);
  Serialize(notify.stopped_thread_koids, writer);
}

void WriteNotifyIO(const NotifyIO& notify, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kNotifyIO, 0);
  writer->WriteUint64(notify.process_koid);
  writer->WriteUint32(static_cast<uint32_t>(notify.type));
  writer->WriteString(notify.data);
  writer->WriteBool(notify.more_data_available);
}

}  // namespace debug_ipc
