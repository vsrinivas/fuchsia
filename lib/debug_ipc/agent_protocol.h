// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_ipc {

class MessageReader;
class MessageWriter;

// Hello.
bool ReadRequest(MessageReader* reader,
                 HelloRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const HelloReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer);

// Launch.
bool ReadRequest(MessageReader* reader,
                 LaunchRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const LaunchReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer);

// Attach.
bool ReadRequest(MessageReader* reader,
                 AttachRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const AttachReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer);

// ProcessTree.
bool ReadRequest(MessageReader* reader,
                 ProcessTreeRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const ProcessTreeReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer);

// Threads.
bool ReadRequest(MessageReader* reader,
                 ThreadsRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const ThreadsReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer);

// ReadMemory.
bool ReadRequest(MessageReader* reader,
                 ReadMemoryRequest* request,
                 uint32_t* transaction_id);
void WriteReply(const ReadMemoryReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer);

// Notifications ---------------------------------------------------------------
//
// (These don't have a "request"/"reply".)

void WriteNotifyThread(MsgHeader::Type type, const NotifyThread& notify,
                       MessageWriter* writer);

}  // namespace debug_ipc
