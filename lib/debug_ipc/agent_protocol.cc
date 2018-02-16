// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/agent_protocol.h"

#include "garnet/lib/debug_ipc/message_reader.h"
#include "garnet/lib/debug_ipc/message_writer.h"

namespace debug_ipc {

namespace {

void WriteProcessTreeRecord(MessageWriter* writer,
                            const ProcessTreeRecord& record) {
  writer->WriteUint32(static_cast<uint32_t>(record.type));
  writer->WriteUint64(record.koid);
  writer->WriteString(record.name);

  uint32_t size = static_cast<uint32_t>(record.children.size());
  writer->WriteUint32(size);
  for (uint32_t i = 0; i < size; i++) {
    WriteProcessTreeRecord(writer, record.children[i]);
  }
}

}  // namespace

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
  WriteProcessTreeRecord(writer, reply.root);
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

  uint32_t size = static_cast<uint32_t>(reply.threads.size());
  writer->WriteUint32(size);
  for (uint32_t i = 0; i < size; i++) {
    writer->WriteUint64(reply.threads[i].koid);
    writer->WriteString(reply.threads[i].name);
  }
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

  uint32_t block_count = static_cast<uint32_t>(reply.blocks.size());
  writer->WriteUint32(block_count);
  for (const auto& block : reply.blocks) {
    writer->WriteUint64(block.address);
    writer->WriteUint32(block.valid ? 1 : 0);
    writer->WriteUint64(block.size);
    if (block.valid && block.size > 0) {
      writer->WriteBytes(&block.data[0], block.size);
    }
  }
}

}  // namespace debug_ipc
