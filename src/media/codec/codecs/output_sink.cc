// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "output_sink.h"

OutputSink::OutputSink(Sender sender, thrd_t writer_thread)
    : sender_(std::move(sender)), writer_thread_(writer_thread) {
  ZX_DEBUG_ASSERT(sender_);
}

void OutputSink::AddOutputPacket(CodecPacket* output_packet) {
  ZX_DEBUG_ASSERT(output_packet);

  if (output_packet->buffer()) {
    free_output_buffers_.Push(std::move(output_packet->buffer()));
  }

  free_output_packets_.Push(std::move(output_packet));
}

void OutputSink::AddOutputBuffer(const CodecBuffer* output_buffer) {
  ZX_DEBUG_ASSERT(output_buffer);

  free_output_buffers_.Push(std::move(output_buffer));
}

OutputSink::Status OutputSink::NextOutputBlock(
    size_t write_size, std::optional<uint64_t> timestamp_ish,
    fit::function<std::pair<size_t, UserStatus>(OutputBlock)> output_block_writer) {
  ZX_DEBUG_ASSERT(thrd_current() == writer_thread_);
  ZX_DEBUG_ASSERT(write_size > 0);

  if (!CurrentPacketHasRoomFor(write_size)) {
    if (SendCurrentPacket() != kOk) {
      return kUserError;
    }

    Status status = SetNewPacketForWrite(write_size);
    if (status != kOk) {
      return status;
    }
  }
  ZX_DEBUG_ASSERT(current_packet_);

  if (current_packet_->valid_length_bytes() == 0 && timestamp_ish) {
    current_packet_->SetTimstampIsh(*timestamp_ish);
  }

  auto output_block = OutputBlock{
      .data = current_packet_->buffer()->base() + current_packet_->valid_length_bytes(),
      .len = write_size,
      .buffer = current_packet_->buffer(),
  };

  auto [bytes_written, write_status] = output_block_writer(output_block);
  if (write_status != kSuccess) {
    return kUserError;
  }

  current_packet_->SetValidLengthBytes(current_packet_->valid_length_bytes() + bytes_written);
  return kOk;
}

OutputSink::Status OutputSink::Flush() {
  ZX_DEBUG_ASSERT(thrd_current() == writer_thread_);

  if (current_packet_ == nullptr || current_packet_->valid_length_bytes() == 0) {
    return kOk;
  }

  return SendCurrentPacket();
}

bool OutputSink::CurrentPacketHasRoomFor(size_t write_size) {
  return (current_packet_ != nullptr) &&
         current_packet_->buffer()->size() - current_packet_->valid_length_bytes() >= write_size;
}

OutputSink::Status OutputSink::SendCurrentPacket() {
  if (!current_packet_) {
    return kOk;
  }
  ZX_DEBUG_ASSERT_MSG(current_packet_->valid_length_bytes() > 0,
                      "Attempting to send empty packet.");

  if (sender_(current_packet_) != kSuccess) {
    return kUserError;
  }

  current_packet_ = nullptr;
  return kOk;
}

OutputSink::Status OutputSink::SetNewPacketForWrite(size_t write_size) {
  auto maybe_buffer = free_output_buffers_.WaitForElement();
  if (!maybe_buffer) {
    return kUserTerminatedWait;
  }
  ZX_DEBUG_ASSERT_MSG(*maybe_buffer, "A null buffer made it into the queue.");
  const CodecBuffer* buffer = *maybe_buffer;

  if (buffer->size() < write_size) {
    return kBuffersTooSmall;
  }

  auto maybe_packet = free_output_packets_.WaitForElement();
  if (!maybe_packet) {
    return kUserTerminatedWait;
  }
  ZX_DEBUG_ASSERT_MSG(*maybe_packet, "A null packet made it into the queue.");
  current_packet_ = *maybe_packet;
  current_packet_->SetBuffer(buffer);
  current_packet_->SetStartOffset(0);
  current_packet_->SetValidLengthBytes(0);

  return kOk;
}

void OutputSink::StopAllWaits() {
  free_output_buffers_.StopAllWaits();
  free_output_packets_.StopAllWaits();
}

void OutputSink::Reset(bool keep_data) {
  free_output_buffers_.Reset(keep_data);
  free_output_packets_.Reset(keep_data);
}
