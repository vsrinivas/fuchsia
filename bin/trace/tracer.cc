// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace/tracer.h"

#include <utility>

#include <fbl/type_support.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <trace-engine/fields.h>
#include <trace-reader/reader.h>

#include "lib/fxl/logging.h"

namespace tracing {
namespace {

// Note: Buffer needs to be big enough to store records of maximum size.
constexpr size_t kReadBufferSize = trace::RecordFields::kMaxRecordSizeBytes * 4;

}  // namespace

Tracer::Tracer(fuchsia::tracing::TraceController* controller)
    : controller_(controller), dispatcher_(nullptr), wait_(this) {
  FXL_DCHECK(controller_);
  wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);
}

Tracer::~Tracer() {
  CloseSocket();
}

void Tracer::Start(fuchsia::tracing::TraceOptions options,
                   RecordConsumer record_consumer,
                   ErrorHandler error_handler,
                   fit::closure start_callback,
                   fit::closure done_callback) {
  FXL_DCHECK(state_ == State::kStopped);

  state_ = State::kStarted;
  done_callback_ = std::move(done_callback);
  start_callback_ = std::move(start_callback);

  zx::socket outgoing_socket;
  zx_status_t status = zx::socket::create(0u, &socket_, &outgoing_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket: status=" << status;
    Done();
    return;
  }

  controller_->StartTracing(std::move(options), std::move(outgoing_socket),
                            [this]() { start_callback_(); });

  buffer_.reserve(kReadBufferSize);
  reader_.reset(new trace::TraceReader(fbl::move(record_consumer),
                                       fbl::move(error_handler)));

  dispatcher_ = async_get_default_dispatcher();
  wait_.set_object(socket_.get());
  status = wait_.Begin(dispatcher_);
  FXL_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;
}

void Tracer::Stop() {
  // Note: The controller will close the socket when finished.
  if (state_ == State::kStarted) {
    state_ = State::kStopping;
    controller_->StopTracing();
  }
}

void Tracer::OnHandleReady(async_dispatcher_t* dispatcher,
                           async::WaitBase* wait,
                           zx_status_t status,
                           const zx_packet_signal_t* signal) {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  if (status != ZX_OK) {
    OnHandleError(status);
    return;
  }

  if (signal->observed & ZX_SOCKET_READABLE) {
    DrainSocket(dispatcher);
  } else if (signal->observed & ZX_SOCKET_PEER_CLOSED) {
    Done();
  } else {
    FXL_CHECK(false);
  }
}

void Tracer::DrainSocket(async_dispatcher_t* dispatcher) {
  for (;;) {
    size_t actual;
    zx_status_t status =
        socket_.read(0u, buffer_.data() + buffer_end_,
                     buffer_.capacity() - buffer_end_, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      status = wait_.Begin(dispatcher);
      if (status != ZX_OK) {
        OnHandleError(status);
      }
      return;
    }

    if (status || actual == 0) {
      if (status != ZX_ERR_PEER_CLOSED) {
        FXL_LOG(ERROR) << "Failed to read data from socket: status=" << status;
      }
      Done();
      return;
    }

    buffer_end_ += actual;
    size_t bytes_available = buffer_end_;
    FXL_DCHECK(bytes_available > 0);

    trace::Chunk chunk(reinterpret_cast<const uint64_t*>(buffer_.data()),
                       trace::BytesToWords(bytes_available));
    if (!reader_->ReadRecords(chunk)) {
      FXL_LOG(ERROR) << "Trace stream is corrupted";
      Done();
      return;
    }

    size_t bytes_consumed =
        bytes_available - trace::WordsToBytes(chunk.remaining_words());
    bytes_available -= bytes_consumed;
    memmove(buffer_.data(), buffer_.data() + bytes_consumed, bytes_available);
    buffer_end_ = bytes_available;
  }
}

void Tracer::OnHandleError(zx_status_t status) {
  FXL_LOG(ERROR) << "Failed to wait on socket: status=" << status;
  Done();
}

void Tracer::CloseSocket() {
  if (socket_) {
    wait_.Cancel();
    wait_.set_object(ZX_HANDLE_INVALID);
    dispatcher_ = nullptr;
    socket_.reset();
  }
}

void Tracer::Done() {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  state_ = State::kStopped;
  reader_.reset();

  CloseSocket();

  if (done_callback_) {
    async::PostTask(async_get_default_dispatcher(), std::move(done_callback_));
  }
}

}  // namespace tracing
