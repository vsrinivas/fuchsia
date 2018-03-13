// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/client_protocol.h"

#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/protocol_helpers.h"

namespace debug_ipc {

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
  return true;
}

bool Deserialize(MessageReader* reader, MemoryBlock* block) {
  if (!reader->ReadUint64(&block->address))
    return false;

  uint32_t valid_flag;
  if (!reader->ReadUint32(&valid_flag))
    return false;
  block->valid = !!valid_flag;

  if (!reader->ReadUint64(&block->size))
    return false;
  if (block->size > reader->remaining())
    return false;
  if (block->valid && block->size > 0) {
    block->data.resize(block->size);
    if (!reader->ReadBytes(block->size, &block->data[0]))
      return false;
  }
  return true;
}

bool Deserialize(MessageReader* reader, NotifyThread* thread) {
  if (!reader->ReadUint64(&thread->process_koid))
    return false;
  return reader->ReadUint64(&thread->thread_koid);
}

// Hello -----------------------------------------------------------------------

void WriteRequest(const HelloRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kHello, transaction_id);
}

bool ReadReply(MessageReader* reader,
               HelloReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(HelloReply), reply);
}

// Launch ----------------------------------------------------------------------

void WriteRequest(const LaunchRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kLaunch, transaction_id);
  Serialize(request.argv, writer);
}

bool ReadReply(MessageReader* reader,
               LaunchReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  if (!reader->ReadUint32(&reply->status))
    return false;
  if (!reader->ReadUint64(&reply->process_koid))
    return false;
  return true;
}

// Attach ----------------------------------------------------------------------

void WriteRequest(const AttachRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kAttach, transaction_id);
  writer->WriteUint64(request.koid);
}

bool ReadReply(MessageReader* reader,
               AttachReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  if (!reader->ReadUint32(&reply->status))
    return false;
  return true;
}

// ProcessTree -----------------------------------------------------------------

void WriteRequest(const ProcessTreeRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kProcessTree, transaction_id);
  writer->WriteBytes(&request, sizeof(ProcessTreeRequest));
}

bool ReadReply(MessageReader* reader,
               ProcessTreeReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return Deserialize(reader, &reply->root);
}

// Threads ---------------------------------------------------------------------

void WriteRequest(const ThreadsRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kThreads, transaction_id);
  writer->WriteBytes(&request, sizeof(ThreadsRequest));
}

bool ReadReply(MessageReader* reader,
               ThreadsReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return Deserialize(reader, &reply->threads);
}

// ReadMemory ------------------------------------------------------------------

void WriteRequest(const ReadMemoryRequest& request,
                  uint32_t transaction_id,
                  MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kReadMemory, transaction_id);
  writer->WriteBytes(&request, sizeof(ReadMemoryRequest));
}

bool ReadReply(MessageReader* reader,
               ReadMemoryReply* reply,
               uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return Deserialize(reader, &reply->blocks);
}

// NotifyThread ----------------------------------------------------------------

bool ReadNotifyThread(MessageReader* reader, NotifyThread* thread) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  return Deserialize(reader, thread);
}

}  // namespace debug_ipc
