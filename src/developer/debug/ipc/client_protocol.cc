// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/client_protocol.h"

#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/ipc/protocol_helpers.h"

#include "lib/fxl/logging.h"

namespace debug_ipc {

// Record deserializers --------------------------------------------------------

bool Deserialize(MessageReader* reader, ProcessTreeRecord* record) {
  if (!reader->ReadUint32(reinterpret_cast<uint32_t*>(&record->type)))
    return false;
  if (!reader->ReadUint64(&record->koid))
    return false;
  if (!reader->ReadString(&record->name))
    return false;
  return Deserialize(reader, &record->children);
}

bool Deserialize(MessageReader* reader, ThreadRecord* record) {
  if (!reader->ReadUint64(&record->koid))
    return false;
  if (!reader->ReadString(&record->name))
    return false;

  uint32_t state;
  if (!reader->ReadUint32(&state))
    return false;
  if (state >= static_cast<uint32_t>(ThreadRecord::State::kLast))
    return false;
  record->state = static_cast<ThreadRecord::State>(state);

  uint32_t blocked_reason;
  if (!reader->ReadUint32(&blocked_reason))
    return false;
  if (state >= static_cast<uint32_t>(ThreadRecord::BlockedReason::kLast))
    return false;
  record->blocked_reason =
      static_cast<ThreadRecord::BlockedReason>(blocked_reason);

  uint32_t stack_amount;
  if (!reader->ReadUint32(&stack_amount))
    return false;
  if (stack_amount >= static_cast<uint32_t>(ThreadRecord::StackAmount::kLast))
    return false;
  record->stack_amount = static_cast<ThreadRecord::StackAmount>(stack_amount);

  if (!Deserialize(reader, &record->frames))
    return false;
  return true;
}

bool Deserialize(MessageReader* reader, MemoryBlock* block) {
  if (!reader->ReadUint64(&block->address))
    return false;
  if (!reader->ReadBool(&block->valid))
    return false;
  if (!reader->ReadUint32(&block->size))
    return false;
  if (block->valid) {
    if (block->size > reader->remaining())
      return false;

    block->data.resize(block->size);
    if (!reader->ReadBytes(block->size, &block->data[0]))
      return false;
  }
  return true;
}

bool Deserialize(MessageReader* reader, Module* module) {
  if (!reader->ReadString(&module->name))
    return false;
  if (!reader->ReadUint64(&module->base))
    return false;
  if (!reader->ReadString(&module->build_id))
    return false;
  return true;
}

bool Deserialize(MessageReader* reader, RegisterCategory* reg_cat) {
  if (!reader->ReadUint32(reinterpret_cast<uint32_t*>(&reg_cat->type)))
    return false;
  return Deserialize(reader, &reg_cat->registers);
}

bool Deserialize(MessageReader* reader, StackFrame* frame) {
  return reader->ReadBytes(sizeof(StackFrame), frame);
}

bool Deserialize(MessageReader* reader, BreakpointStats* stats) {
  if (!reader->ReadUint32(&stats->breakpoint_id))
    return false;
  if (!reader->ReadUint32(&stats->hit_count))
    return false;
  return reader->ReadBool(&stats->should_delete);
}

bool Deserialize(MessageReader* reader, AddressRegion* region) {
  if (!reader->ReadString(&region->name))
    return false;
  if (!reader->ReadUint64(&region->base))
    return false;
  if (!reader->ReadUint64(&region->size))
    return false;
  if (!reader->ReadUint64(&region->depth))
    return false;
  return true;
}

// Record serializers ----------------------------------------------------------

void Serialize(const ProcessBreakpointSettings& settings,
               MessageWriter* writer) {
  writer->WriteUint64(settings.process_koid);
  writer->WriteUint64(settings.thread_koid);
  writer->WriteUint64(settings.address);
}

void Serialize(const BreakpointSettings& settings, MessageWriter* writer) {
  writer->WriteUint32(settings.breakpoint_id);
  writer->WriteBool(settings.one_shot);
  writer->WriteUint32(static_cast<uint32_t>(settings.stop));
  writer->WriteUint32(static_cast<uint32_t>(settings.type));
  Serialize(settings.locations, writer);
}

void Serialize(const RegisterCategory::Type& type, MessageWriter* writer) {
  writer->WriteUint32(static_cast<uint32_t>(type));
}

// Hello -----------------------------------------------------------------------

void WriteRequest(const HelloRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kHello, transaction_id);
}

bool ReadReply(MessageReader* reader, HelloReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(HelloReply), reply);
}

// Launch ----------------------------------------------------------------------

void WriteRequest(const LaunchRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kLaunch, transaction_id);
  writer->WriteUint32(static_cast<uint32_t>(request.inferior_type));
  Serialize(request.argv, writer);
}

bool ReadReply(MessageReader* reader, LaunchReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  uint32_t inferior_type;
  if (!reader->ReadUint32(&inferior_type) ||
      inferior_type >= static_cast<uint32_t>(InferiorType::kLast)) {
    return false;
  }
  reply->inferior_type = static_cast<InferiorType>(inferior_type);

  if (!reader->ReadInt32(&reply->status))
    return false;
  if (!reader->ReadUint64(&reply->process_id))
    return false;
  if (!reader->ReadUint64(&reply->component_id))
    return false;
  if (!reader->ReadString(&reply->process_name))
    return false;
  return true;
}

// Kill ----------------------------------------------------------------------

void WriteRequest(const KillRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kKill, transaction_id);
  writer->WriteUint64(request.process_koid);
}

bool ReadReply(MessageReader* reader, KillReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  if (!reader->ReadInt32(&reply->status))
    return false;
  return true;
}

// Attach ----------------------------------------------------------------------

void WriteRequest(const AttachRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAttach, transaction_id);
  writer->WriteUint32(static_cast<uint32_t>(request.type));
  writer->WriteUint64(request.koid);
}

bool ReadReply(MessageReader* reader, AttachReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  if (!reader->ReadUint64(&reply->koid))
    return false;
  if (!reader->ReadInt32(&reply->status))
    return false;
  if (!reader->ReadString(&reply->name))
    return false;
  return true;
}

// Detach ----------------------------------------------------------------------

void WriteRequest(const DetachRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kDetach, transaction_id);
  writer->WriteUint32(static_cast<uint32_t>(request.type));
  writer->WriteUint64(request.koid);
}

bool ReadReply(MessageReader* reader, DetachReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  if (!reader->ReadInt32(&reply->status))
    return false;
  return true;
}

// Pause -----------------------------------------------------------------------

void WriteRequest(const PauseRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kPause, transaction_id);
  writer->WriteUint64(request.process_koid);
  writer->WriteUint64(request.thread_koid);
}

bool ReadReply(MessageReader* reader, PauseReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

// QuitAgent -------------------------------------------------------------------

void WriteRequest(const QuitAgentRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kQuitAgent, transaction_id);
}

bool ReadReply(MessageReader* reader, QuitAgentReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

// Resume ----------------------------------------------------------------------

void WriteRequest(const ResumeRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kResume, transaction_id);
  writer->WriteUint64(request.process_koid);
  Serialize(request.thread_koids, writer);
  writer->WriteUint32(static_cast<uint32_t>(request.how));
  writer->WriteUint64(request.range_begin);
  writer->WriteUint64(request.range_end);
}

bool ReadReply(MessageReader* reader, ResumeReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

// ProcessTree -----------------------------------------------------------------

void WriteRequest(const ProcessTreeRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kProcessTree, transaction_id);
  writer->WriteBytes(&request, sizeof(ProcessTreeRequest));
}

bool ReadReply(MessageReader* reader, ProcessTreeReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return Deserialize(reader, &reply->root);
}

// Threads ---------------------------------------------------------------------

void WriteRequest(const ThreadsRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kThreads, transaction_id);
  writer->WriteBytes(&request, sizeof(ThreadsRequest));
}

bool ReadReply(MessageReader* reader, ThreadsReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return Deserialize(reader, &reply->threads);
}

// ReadMemory ------------------------------------------------------------------

void WriteRequest(const ReadMemoryRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kReadMemory, transaction_id);
  writer->WriteBytes(&request, sizeof(ReadMemoryRequest));
}

bool ReadReply(MessageReader* reader, ReadMemoryReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return Deserialize(reader, &reply->blocks);
}

// ReadRegisters ---------------------------------------------------------------

void WriteRequest(const ReadRegistersRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kReadRegisters, transaction_id);

  writer->WriteUint64(request.process_koid);
  writer->WriteUint64(request.thread_koid);
  Serialize(request.categories, writer);
}

bool ReadReply(MessageReader* reader, ReadRegistersReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;

  *transaction_id = header.transaction_id;
  return Deserialize(reader, &reply->categories);
}

// WriteRegisters --------------------------------------------------------------

void WriteRequest(const WriteRegistersRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kWriteRegisters, transaction_id);
  writer->WriteUint64(request.process_koid);
  writer->WriteUint64(request.thread_koid);
  Serialize(request.registers, writer);
}

bool ReadReply(MessageReader* reader, WriteRegistersReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;

  *transaction_id = header.transaction_id;
  return reader->ReadInt32(&reply->status);
}

// AddOrChangeBreakpoint -------------------------------------------------------

void WriteRequest(const AddOrChangeBreakpointRequest& request,
                  uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAddOrChangeBreakpoint, transaction_id);
  Serialize(request.breakpoint, writer);
}

bool ReadReply(MessageReader* reader, AddOrChangeBreakpointReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return reader->ReadInt32(&reply->status);
}

// RemoveBreakpoint ------------------------------------------------------------

void WriteRequest(const RemoveBreakpointRequest& request,
                  uint32_t transaction_id, MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kRemoveBreakpoint, transaction_id);
  writer->WriteBytes(&request, sizeof(RemoveBreakpointRequest));
}

bool ReadReply(MessageReader* reader, RemoveBreakpointReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

// ThreadStatus ----------------------------------------------------------------

void WriteRequest(const ThreadStatusRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kThreadStatus, transaction_id);
  writer->WriteBytes(&request, sizeof(ThreadStatusRequest));
}

bool ReadReply(MessageReader* reader, ThreadStatusReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  Deserialize(reader, &reply->record);
  return true;
}

// Modules ---------------------------------------------------------------------

void WriteRequest(const ModulesRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kModules, transaction_id);
  writer->WriteBytes(&request, sizeof(ModulesRequest));
}

bool ReadReply(MessageReader* reader, ModulesReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  Deserialize(reader, &reply->modules);
  return true;
}

// Address Space --------------------------------------------------------------

void WriteRequest(const AddressSpaceRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAddressSpace, transaction_id);
  writer->WriteBytes(&request, sizeof(AddressSpaceRequest));
}

bool ReadReply(MessageReader* reader, AddressSpaceReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return Deserialize(reader, &reply->map);
}

// JobFilter ------------------------------------------------------------------

void WriteRequest(const JobFilterRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kJobFilter, transaction_id);
  writer->WriteUint64(request.job_koid);
  return Serialize(request.filters, writer);
}

bool ReadReply(MessageReader* reader, JobFilterReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return reader->ReadInt32(&reply->status);
}

// WriteMemory -----------------------------------------------------------------

void WriteRequest(const WriteMemoryRequest& request, uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kWriteMemory, transaction_id);
  writer->WriteUint64(request.process_koid);
  writer->WriteUint64(request.address);
  return Serialize(request.data, writer);
}

bool ReadReply(MessageReader* reader, WriteMemoryReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return reader->ReadInt32(&reply->status);
}

// Notifications ---------------------------------------------------------------

bool ReadNotifyProcessExiting(MessageReader* reader,
                              NotifyProcessExiting* process) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  if (!reader->ReadUint64(&process->process_koid))
    return false;
  if (!reader->ReadInt64(&process->return_code))
    return false;
  return true;
}

bool ReadNotifyProcessStarting(MessageReader* reader,
                               NotifyProcessStarting* process) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  if (!reader->ReadUint64(&process->koid))
    return false;
  if (!reader->ReadUint32(&process->component_id))
    return false;
  if (!reader->ReadString(&process->name))
    return false;
  return true;
}

bool ReadNotifyThread(MessageReader* reader, NotifyThread* notify) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  if (!reader->ReadUint64(&notify->process_koid))
    return false;
  return Deserialize(reader, &notify->record);
}

bool ReadNotifyException(MessageReader* reader, NotifyException* notify) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  if (!reader->ReadUint64(&notify->process_koid))
    return false;
  if (!Deserialize(reader, &notify->thread))
    return false;

  uint32_t type;
  if (!reader->ReadUint32(&type) ||
      type >= static_cast<uint32_t>(NotifyException::Type::kLast))
    return false;
  notify->type = static_cast<NotifyException::Type>(type);

  return Deserialize(reader, &notify->hit_breakpoints);
}

bool ReadNotifyModules(MessageReader* reader, NotifyModules* notify) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  if (!reader->ReadUint64(&notify->process_koid))
    return false;

  if (!Deserialize(reader, &notify->modules))
    return false;
  return Deserialize(reader, &notify->stopped_thread_koids);
}

}  // namespace debug_ipc
