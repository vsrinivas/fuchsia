// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_IPC_CLIENT_PROTOCOL_H_
#define SRC_DEVELOPER_DEBUG_IPC_CLIENT_PROTOCOL_H_

#include "src/developer/debug/ipc/protocol.h"

namespace debug_ipc {

class MessageReader;
class MessageWriter;

// Hello.
void WriteRequest(const HelloRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, HelloReply* reply, uint32_t* transaction_id);

// Status.
void WriteRequest(const StatusRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, StatusReply* reply, uint32_t* transaction_id);

// Launch.
void WriteRequest(const LaunchRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, LaunchReply* reply, uint32_t* transaction_id);

// Stop.
void WriteRequest(const KillRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, KillReply* reply, uint32_t* transaction_id);

// Attach.
void WriteRequest(const AttachRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, AttachReply* reply, uint32_t* transaction_id);

// Detach.
void WriteRequest(const DetachRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, DetachReply* reply, uint32_t* transaction_id);

// Pause.
void WriteRequest(const PauseRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, PauseReply* reply, uint32_t* transaction_id);

// QuitAgent.
void WriteRequest(const QuitAgentRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, QuitAgentReply* reply, uint32_t* transaction_id);

// Resume.
void WriteRequest(const ResumeRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, ResumeReply* reply, uint32_t* transaction_id);

// ProcessTree.
void WriteRequest(const ProcessTreeRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ProcessTreeReply* reply, uint32_t* transaction_id);

// Threads.
void WriteRequest(const ThreadsRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, ThreadsReply* reply, uint32_t* transaction_id);

// ReadMemory.
void WriteRequest(const ReadMemoryRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, ReadMemoryReply* reply, uint32_t* transaction_id);

// ReadRegisters
void WriteRequest(const ReadRegistersRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ReadRegistersReply* reply, uint32_t* transaction_id);

// WriteRegisters
void WriteRequest(const WriteRegistersRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, WriteRegistersReply* reply, uint32_t* transaction_id);

// AddOrChangeBreakpoint.
void WriteRequest(const AddOrChangeBreakpointRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, AddOrChangeBreakpointReply* reply, uint32_t* transaction_id);

// RemoveBreakpoint.
void WriteRequest(const RemoveBreakpointRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, RemoveBreakpointReply* reply, uint32_t* transaction_id);

// SysInfo
void WriteRequest(const SysInfoRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, SysInfoReply* reply, uint32_t* transaction_id);

// ProcessStatus.
void WriteRequest(const ProcessStatusRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ProcessStatusReply* reply, uint32_t* transaction_id);

// ThreadStatus.
void WriteRequest(const ThreadStatusRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ThreadStatusReply* reply, uint32_t* transaction_id);

// Modules.
void WriteRequest(const ModulesRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, ModulesReply* reply, uint32_t* transaction_id);

// Address space.
void WriteRequest(const AddressSpaceRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, AddressSpaceReply* reply, uint32_t* transaction_id);

// JobFilter.
void WriteRequest(const JobFilterRequest& request, uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, JobFilterReply* reply, uint32_t* transaction_id);

// WriteMemory.
void WriteRequest(const WriteMemoryRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, WriteMemoryReply* reply, uint32_t* transaction_id);

// LoadInfoHandleTable.
void WriteRequest(const LoadInfoHandleTableRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, LoadInfoHandleTableReply* reply, uint32_t* transaction_id);

// ConfigAgent.
void WriteRequest(const ConfigAgentRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ConfigAgentReply* reply, uint32_t* transaction_id);

// Notifications ---------------------------------------------------------------
//
// (These don't have a "request"/"reply".)

bool ReadNotifyProcessExiting(MessageReader*, NotifyProcessExiting*);
bool ReadNotifyProcessStarting(MessageReader*, NotifyProcessStarting*);
bool ReadNotifyThread(MessageReader*, NotifyThread*);
bool ReadNotifyException(MessageReader*, NotifyException*);
bool ReadNotifyModules(MessageReader*, NotifyModules*);
bool ReadNotifyIO(MessageReader*, NotifyIO*);

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_IPC_CLIENT_PROTOCOL_H_
