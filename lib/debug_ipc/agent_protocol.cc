// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/agent_protocol.h"

#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"
#include "garnet/lib/debug_ipc/protocol_helpers.h"

namespace debug_ipc {

void Serialize(const ProcessTreeRecord& record, MessageWriter* writer) {
  writer->WriteUint32(static_cast<uint32_t>(record.type));
  writer->WriteUint64(record.koid);
  writer->WriteString(record.name);
  Serialize(record.children, writer);
}

void Serialize(const ThreadRecord& record, MessageWriter* writer) {
  writer->WriteUint64(record.koid);
  writer->WriteString(record.name);
}

void Serialize(const MemoryBlock& block, MessageWriter* writer) {
  writer->WriteUint64(block.address);
  writer->WriteUint32(block.valid ? 1 : 0);
  writer->WriteUint64(block.size);
  if (block.valid && block.size > 0)
    writer->WriteBytes(&block.data[0], block.size);
}

bool ReadRequest(MessageReader* reader,
                 HelloRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return true;
}

void WriteReply(const HelloReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kHello, transaction_id);
  writer->WriteBytes(&reply, sizeof(HelloReply));
}

bool ReadRequest(MessageReader* reader,
                 LaunchRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;

  return Deserialize(reader, &request->argv);
}

void WriteReply(const LaunchReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kLaunch, transaction_id);
  writer->WriteUint32(reply.status);
  writer->WriteUint64(reply.process_koid);
}

bool ReadRequest(MessageReader* reader,
                 ProcessTreeRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ProcessTreeRequest), request);
}

void WriteReply(const ProcessTreeReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kProcessTree, transaction_id);
  Serialize(reply.root, writer);
}

bool ReadRequest(MessageReader* reader,
                 ThreadsRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ThreadsRequest), request);
}

void WriteReply(const ThreadsReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kThreads, transaction_id);

  Serialize(reply.threads, writer);
}

bool ReadRequest(MessageReader* reader,
                 ReadMemoryRequest* request,
                 uint32_t* transaction_id) {
  MsgHeader header;
  if (!reader->ReadHeader(&header))
    return false;
  *transaction_id = header.transaction_id;
  return reader->ReadBytes(sizeof(ReadMemoryRequest), request);
}

void WriteReply(const ReadMemoryReply& reply,
                uint32_t transaction_id,
                MessageWriter* writer) {
  writer->WriteHeader(MsgHeader::Type::kReadMemory, transaction_id);
  Serialize(reply.blocks, writer);
}

}  // namespace debug_ipc
