// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides the FIDL interface for "trace record".

#include "garnet/bin/trace/tracer.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include <utility>

#include <trace-engine/fields.h>
#include <trace-reader/reader.h>

#include "src/lib/fxl/logging.h"

namespace tracing {

Tracer::Tracer(controller::Controller* controller)
    : controller_(controller), dispatcher_(nullptr), wait_(this) {
  FXL_DCHECK(controller_);
  wait_.set_trigger(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED);
}

Tracer::~Tracer() { CloseSocket(); }

void Tracer::Initialize(controller::TraceConfig config, bool binary, BytesConsumer bytes_consumer,
                        RecordConsumer record_consumer, ErrorHandler error_handler,
                        FailCallback fail_callback, DoneCallback done_callback) {
  FXL_DCHECK(state_ == State::kReady);

  zx::socket outgoing_socket;
  zx_status_t status = zx::socket::create(0u, &socket_, &outgoing_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket: status=" << status;
    Fail();
    return;
  }

  controller_->InitializeTracing(std::move(config), std::move(outgoing_socket));

  binary_ = binary;
  bytes_consumer_ = std::move(bytes_consumer);
  reader_.reset(new trace::TraceReader(std::move(record_consumer), std::move(error_handler)));

  fail_callback_ = std::move(fail_callback);
  done_callback_ = std::move(done_callback);

  dispatcher_ = async_get_default_dispatcher();
  wait_.set_object(socket_.get());
  status = wait_.Begin(dispatcher_);
  FXL_CHECK(status == ZX_OK) << "Failed to add handler: status=" << status;

  state_ = State::kInitialized;
}

void Tracer::Start(StartCallback start_callback) {
  FXL_DCHECK(state_ == State::kInitialized);

  start_callback_ = std::move(start_callback);

  // All our categories are passed when we initialize, and we're just
  // starting tracing so the buffer is already empty, so there's nothing to
  // pass for |StartOptions| here.
  controller::StartOptions start_options{};

  controller_->StartTracing(std::move(start_options),
                            [this](controller::Controller_StartTracing_Result result) {
                              start_callback_(std::move(result));
                            });

  state_ = State::kStarted;
}

void Tracer::Terminate() {
  // Note: The controller will close the consumer socket when finished.
  FXL_DCHECK(state_ != State::kReady);
  state_ = State::kTerminating;
  controller::TerminateOptions terminate_options{};
  terminate_options.set_write_results(true);
  controller_->TerminateTracing(std::move(terminate_options),
                                [](controller::TerminateResult result) {
                                  // TODO(dje): Print provider stats.
                                });
}

void Tracer::OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                           zx_status_t status, const zx_packet_signal_t* signal) {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kTerminating);

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
        socket_.read(0u, buffer_.data() + buffer_end_, buffer_.size() - buffer_end_, &actual);
    if (status == ZX_ERR_SHOULD_WAIT) {
      status = wait_.Begin(dispatcher);
      if (status != ZX_OK) {
        OnHandleError(status);
      }
      return;
    }

    if (status || actual == 0) {
      if (status == ZX_ERR_PEER_CLOSED) {
        Done();
      } else {
        FXL_LOG(ERROR) << "Failed to read data from socket: status=" << status;
        Fail();
      }
      return;
    }

    buffer_end_ += actual;
    size_t bytes_available = buffer_end_;
    FXL_DCHECK(bytes_available > 0);

    size_t bytes_consumed;
    if (binary_) {
      bytes_consumer_(buffer_.data(), bytes_available);
      bytes_consumed = bytes_available;
    } else {
      trace::Chunk chunk(reinterpret_cast<const uint64_t*>(buffer_.data()),
                         trace::BytesToWords(bytes_available));
      if (!reader_->ReadRecords(chunk)) {
        FXL_LOG(ERROR) << "Trace stream is corrupted";
        Fail();
        return;
      }
      bytes_consumed = bytes_available - trace::WordsToBytes(chunk.remaining_words());
    }

    bytes_available -= bytes_consumed;
    memmove(buffer_.data(), buffer_.data() + bytes_consumed, bytes_available);
    buffer_end_ = bytes_available;
  }
}

void Tracer::OnHandleError(zx_status_t status) {
  FXL_LOG(ERROR) << "Failed to wait on socket: status=" << status;
  Fail();
}

void Tracer::CloseSocket() {
  if (socket_) {
    wait_.Cancel();
    wait_.set_object(ZX_HANDLE_INVALID);
    dispatcher_ = nullptr;
    socket_.reset();
  }
}

void Tracer::Fail() { fail_callback_(); }

void Tracer::Done() {
  FXL_DCHECK(state_ == State::kStarted || state_ == State::kTerminating);

  state_ = State::kTerminated;
  reader_.reset();

  CloseSocket();

  // TODO(dje): Watch for errors finishing writing of the trace file.

  if (done_callback_) {
    async::PostTask(async_get_default_dispatcher(), std::move(done_callback_));
  }
}

}  // namespace tracing
