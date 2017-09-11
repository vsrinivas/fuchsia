// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/trace/tracer.h"

#include <utility>

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/lib/trace/reader.h"
#include "lib/fxl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

using namespace tracing::internal;

namespace tracing {
namespace {

// Note: Buffer needs to be big enough to store records of maximum size.
constexpr size_t kReadBufferSize = RecordFields::kMaxRecordSizeBytes * 4;

}  // namespace

Tracer::Tracer(TraceController* controller) : controller_(controller) {
  FXL_DCHECK(controller_);
}

Tracer::~Tracer() {
  CloseSocket();
}

void Tracer::Start(TraceOptionsPtr options,
                   RecordConsumer record_consumer,
                   ErrorHandler error_handler,
                   fxl::Closure start_callback,
                   fxl::Closure done_callback) {
  FXL_DCHECK(state_ == State::kStopped);

  state_ = State::kStarted;
  done_callback_ = std::move(done_callback);
  start_callback_ = std::move(start_callback);

  mx::socket outgoing_socket;
  mx_status_t status = mx::socket::create(0u, &socket_, &outgoing_socket);
  if (status != MX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket: status=" << status;
    Done();
    return;
  }

  controller_->StartTracing(std::move(options), std::move(outgoing_socket),
                            [this]() { start_callback_(); });

  buffer_.reserve(kReadBufferSize);
  reader_.reset(new reader::TraceReader(record_consumer, error_handler));

  handler_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
      this, socket_.get(), MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED);
}

void Tracer::Stop() {
  // Note: The controller will close the socket when finished.
  if (state_ == State::kStarted) {
    state_ = State::kStopping;
    controller_->StopTracing();
  }
}

void Tracer::OnHandleReady(mx_handle_t handle,
                           mx_signals_t pending,
                           uint64_t count) {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  if (pending & MX_SOCKET_READABLE) {
    DrainSocket();
  } else if (pending & MX_SOCKET_PEER_CLOSED) {
    Done();
  } else {
    FXL_CHECK(false);
  }
}

void Tracer::DrainSocket() {
  for (;;) {
    size_t actual;
    mx_status_t status =
        socket_.read(0u, buffer_.data() + buffer_end_,
                     buffer_.capacity() - buffer_end_, &actual);
    if (status == MX_ERR_SHOULD_WAIT)
      return;

    if (status || actual == 0) {
      if (status != MX_ERR_PEER_CLOSED) {
        FXL_LOG(ERROR) << "Failed to read data from socket: status=" << status;
      }
      Done();
      return;
    }

    buffer_end_ += actual;
    size_t bytes_available = buffer_end_;
    FXL_DCHECK(bytes_available > 0);

    reader::Chunk chunk(reinterpret_cast<const uint64_t*>(buffer_.data()),
                        BytesToWords(bytes_available));
    if (!reader_->ReadRecords(chunk)) {
      FXL_LOG(ERROR) << "Trace stream is corrupted";
      Done();
      return;
    }

    size_t bytes_consumed =
        bytes_available - WordsToBytes(chunk.remaining_words());
    bytes_available -= bytes_consumed;
    memmove(buffer_.data(), buffer_.data() + bytes_consumed, bytes_available);
    buffer_end_ = bytes_available;
  }
}

void Tracer::CloseSocket() {
  if (socket_) {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(handler_key_);
    socket_.reset();
  }
}

void Tracer::Done() {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  state_ = State::kStopped;
  reader_.reset();

  CloseSocket();

  if (done_callback_) {
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        std::move(done_callback_));
  }
}

}  // namespace tracing
