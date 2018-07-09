// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_ipc {

class MessageReader;
class MessageWriter;

// Hello.
void WriteRequest(const HelloRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, HelloReply* reply,
               uint32_t* transaction_id);

// Launch.
void WriteRequest(const LaunchRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, LaunchReply* reply,
               uint32_t* transaction_id);

// Stop.
void WriteRequest(const KillRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, KillReply* reply,
               uint32_t* transaction_id);

// Attach.
void WriteRequest(const AttachRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, AttachReply* reply,
               uint32_t* transaction_id);

// Detach.
void WriteRequest(const DetachRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, DetachReply* reply,
               uint32_t* transaction_id);

// Pause.
void WriteRequest(const PauseRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, PauseReply* reply,
               uint32_t* transaction_id);

// Resume.
void WriteRequest(const ResumeRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ResumeReply* reply,
               uint32_t* transaction_id);

// ProcessTree.
void WriteRequest(const ProcessTreeRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ProcessTreeReply* reply,
               uint32_t* transaction_id);

// Threads.
void WriteRequest(const ThreadsRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ThreadsReply* reply,
               uint32_t* transaction_id);

// ReadMemory.
void WriteRequest(const ReadMemoryRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ReadMemoryReply* reply,
               uint32_t* transaction_id);

// Registers
void WriteRequest(const RegistersRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, RegistersReply* reply,
               uint32_t* transaction_id);

// AddOrChangeBreakpoint.
void WriteRequest(const AddOrChangeBreakpointRequest& request,
                  uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, AddOrChangeBreakpointReply* reply,
               uint32_t* transaction_id);

// RemoveBreakpoint.
void WriteRequest(const RemoveBreakpointRequest& request,
                  uint32_t transaction_id, MessageWriter* writer);
bool ReadReply(MessageReader* reader, RemoveBreakpointReply* reply,
               uint32_t* transaction_id);

// Backtrace.
void WriteRequest(const BacktraceRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, BacktraceReply* reply,
               uint32_t* transaction_id);

// Modules.
void WriteRequest(const ModulesRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, ModulesReply* reply,
               uint32_t* transaction_id);

// Address space.
void WriteRequest(const AddressSpaceRequest& request, uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader, AddressSpaceReply* reply,
               uint32_t* transaction_id);

// Notifications ---------------------------------------------------------------
//
// (These don't have a "request"/"reply".)

bool ReadNotifyProcess(MessageReader* reader, NotifyProcess* notify);
bool ReadNotifyThread(MessageReader* reader, NotifyThread* notify);
bool ReadNotifyException(MessageReader* reader, NotifyException* notify);
bool ReadNotifyModules(MessageReader* reader, NotifyModules* notify);

}  // namespace debug_ipc
