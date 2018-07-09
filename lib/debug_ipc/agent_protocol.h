// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_ipc {

class MessageReader;
class MessageWriter;

// Hello.
bool ReadRequest(MessageReader* reader, HelloRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const HelloReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Launch.
bool ReadRequest(MessageReader* reader, LaunchRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const LaunchReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Stop.
bool ReadRequest(MessageReader* reader, KillRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const KillReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Attach.
bool ReadRequest(MessageReader* reader, AttachRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const AttachReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Detach.
bool ReadRequest(MessageReader* reader, DetachRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const DetachReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Pause.
bool ReadRequest(MessageReader* reader, PauseRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const PauseReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Resume.
bool ReadRequest(MessageReader* reader, ResumeRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const ResumeReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// ProcessTree.
bool ReadRequest(MessageReader* reader, ProcessTreeRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const ProcessTreeReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Threads.
bool ReadRequest(MessageReader* reader, ThreadsRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const ThreadsReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// ReadMemory.
bool ReadRequest(MessageReader* reader, ReadMemoryRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const ReadMemoryReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// AddOrChangeBreakpoint.
bool ReadRequest(MessageReader* reader, AddOrChangeBreakpointRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const AddOrChangeBreakpointReply& reply,
                uint32_t transaction_id, MessageWriter* writer);

// RemoveBreakpoint.
bool ReadRequest(MessageReader* reader, RemoveBreakpointRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const RemoveBreakpointReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Backtrace
bool ReadRequest(MessageReader* reader, BacktraceRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const BacktraceReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Modules
bool ReadRequest(MessageReader* reader, ModulesRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const ModulesReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Registers
bool ReadRequest(MessageReader* reader, RegistersRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const RegistersReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Address space
bool ReadRequest(MessageReader* reader, AddressSpaceRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const AddressSpaceReply& reply, uint32_t transaction_id,
                MessageWriter* writer);

// Notifications ---------------------------------------------------------------
//
// (These don't have a "request"/"reply".)

void WriteNotifyProcess(const NotifyProcess& notify, MessageWriter* writer);
void WriteNotifyThread(MsgHeader::Type type, const NotifyThread& notify,
                       MessageWriter* writer);
void WriteNotifyException(const NotifyException& notify, MessageWriter* writer);
void WriteNotifyModules(const NotifyModules& notify, MessageWriter* writer);

}  // namespace debug_ipc
