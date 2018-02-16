// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/client_protocol.h"

#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"

namespace debug_ipc {

namespace {

bool ReadProcessTreeRecord(MessageReader* reader, ProcessTreeRecord* record) {
  if (!reader->ReadUint32(reinterpret_cast<uint32_t*>(&record->type)))
    return false;
  if (!reader->ReadUint64(&record->koid))
    return false;
  if (!reader->ReadString(&record->name))
    return false;

  uint32_t size = 0;
  if (!reader->ReadUint32(&size))
    return false;
  record->children.resize(size);
  for (uint32_t i = 0; i < size; i++) {
    if (!ReadProcessTreeRecord(reader, &record->children[i]))
      return false;
  }
  return true;
}

}  // namespace

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
  return ReadProcessTreeRecord(reader, &reply->root);
}

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

  uint32_t size = 0;
  if (!reader->ReadUint32(&size))
    return false;
  reply->threads.resize(size);
  for (uint32_t i = 0; i < size; i++) {
    ThreadRecord* thread = &reply->threads[i];
    if (!reader->ReadUint64(&thread->koid))
      return false;
    if (!reader->ReadString(&thread->name))
      return false;
  }
  return true;
}

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

  uint32_t block_count = 0;
  if (!reader->ReadUint32(&block_count))
    return false;
  reply->blocks.resize(block_count);
  for (auto& block : reply->blocks) {
    if (!reader->ReadUint64(&block.address))
      return false;

    uint32_t valid_flag;
    if (!reader->ReadUint32(&valid_flag))
      return false;
    block.valid = !!valid_flag;

    if (!reader->ReadUint64(&block.size))
      return false;
    if (block.size > reader->remaining())
      return false;
    if (block.valid && block.size > 0) {
      block.data.resize(block.size);
      if (!reader->ReadBytes(block.size, &block.data[0]))
        return false;
    }
  }
  return true;
}

}  // namespace debug_ipc
