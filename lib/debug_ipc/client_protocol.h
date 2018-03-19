// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/protocol.h"

namespace debug_ipc {

class MessageReader;
class MessageWriter;

// Hello.
void WriteRequest(const HelloRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader,
               HelloReply* reply,
               uint32_t* transaction_id);

// Launch.
void WriteRequest(const LaunchRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader,
               LaunchReply* reply,
               uint32_t* transaction_id);

// Attach.
void WriteRequest(const AttachRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader,
               AttachReply* reply,
               uint32_t* transaction_id);

// Detach.
void WriteRequest(const DetachRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader,
               DetachReply* reply,
               uint32_t* transaction_id);

// Continue.
void WriteRequest(const ContinueRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader,
               ContinueReply* reply,
               uint32_t* transaction_id);

// ProcessTree.
void WriteRequest(const ProcessTreeRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader,
               ProcessTreeReply* reply,
               uint32_t* transaction_id);

// Threads.
void WriteRequest(const ThreadsRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader,
               ThreadsReply* reply,
               uint32_t* transaction_id);

// ReadMemory.
void WriteRequest(const ReadMemoryRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer);
bool ReadReply(MessageReader* reader,
               ReadMemoryReply* reply,
               uint32_t* transaction_id);

// Notifications ---------------------------------------------------------------
//
// (These don't have a "request"/"reply".)

bool ReadNotifyProcess(MessageReader* reader, NotifyProcess* notify);
bool ReadNotifyThread(MessageReader* reader, NotifyThread* notify);
bool ReadNotifyException(MessageReader* reader, NotifyException* notify);

}  // namespace debug_ipc
