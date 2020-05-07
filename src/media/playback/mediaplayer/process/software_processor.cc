// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/process/software_processor.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include "src/media/playback/mediaplayer/graph/formatting.h"
#include "src/media/playback/mediaplayer/graph/thread_priority.h"

namespace media_player {

SoftwareProcessor::SoftwareProcessor()
    : main_thread_dispatcher_(async_get_default_dispatcher()),
      worker_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  output_state_ = OutputState::kIdle;
  worker_loop_.StartThread();

  PostTaskToWorkerThread([]() { ThreadPriority::SetToHigh(); });
}

SoftwareProcessor::~SoftwareProcessor() { FX_DCHECK(is_main_thread()); }

void SoftwareProcessor::FlushInput(bool hold_frame, size_t input_index, fit::closure callback) {
  FX_DCHECK(is_main_thread());
  FX_DCHECK(input_index == 0);
  FX_DCHECK(callback);

  flushing_ = true;
  input_packet_ = nullptr;
  end_of_input_stream_ = false;

  // If we were waiting for an input packet, we aren't anymore.
  if (output_state_ == OutputState::kWaitingForInput) {
    output_state_ = OutputState::kIdle;
  }

  callback();
}

void SoftwareProcessor::FlushOutput(size_t output_index, fit::closure callback) {
  FX_DCHECK(is_main_thread());
  FX_DCHECK(output_index == 0);
  FX_DCHECK(callback);

  flushing_ = true;
  end_of_output_stream_ = false;

  if (output_state_ == OutputState::kWaitingForWorker ||
      output_state_ == OutputState::kWorkerNotDone) {
    // The worker is busy processing an input packet. Wait until it's done
    // before calling the callback.
    flush_callback_ = std::move(callback);
    return;
  }

  PostTaskToWorkerThread([this, callback = std::move(callback)]() mutable {
    Flush();
    PostTaskToMainThread(std::move(callback));
  });
}

void SoftwareProcessor::PutInputPacket(PacketPtr packet, size_t input_index) {
  FX_DCHECK(is_main_thread());
  FX_DCHECK(packet);
  FX_DCHECK(input_index == 0);

  FX_DCHECK(!input_packet_);
  FX_DCHECK(!end_of_input_stream_);

  if (flushing_) {
    // We're flushing. Discard the packet.
    return;
  }

  if (packet->end_of_stream()) {
    end_of_input_stream_ = true;
  }

  if (output_state_ != OutputState::kWaitingForInput) {
    // We weren't waiting for this packet, so save it for later.
    input_packet_ = std::move(packet);
    return;
  }

  output_state_ = OutputState::kWaitingForWorker;

  PostTaskToWorkerThread([this, packet] { HandleInputPacketOnWorker(packet); });

  if (!end_of_input_stream_) {
    // Request another packet to keep |input_packet_| full.
    RequestInputPacket();
  }
}

void SoftwareProcessor::RequestOutputPacket() {
  FX_DCHECK(is_main_thread());
  FX_DCHECK(!end_of_output_stream_);

  if (flushing_) {
    FX_DCHECK(!end_of_input_stream_);
    FX_DCHECK(!input_packet_);
    flushing_ = false;
    RequestInputPacket();
  }

  if (output_state_ == OutputState::kWaitingForWorker) {
    return;
  }

  if (output_state_ == OutputState::kWorkerNotDone) {
    // The worker is processing an input packet and has satisfied a previous
    // request for an output packet. Indicate that we have a new unsatisfied
    // request.
    output_state_ = OutputState::kWaitingForWorker;
    return;
  }

  if (!input_packet_) {
    FX_DCHECK(!end_of_input_stream_);

    // We're expecting an input packet. Wait for it.
    output_state_ = OutputState::kWaitingForInput;
    return;
  }

  output_state_ = OutputState::kWaitingForWorker;

  PostTaskToWorkerThread(
      [this, packet = std::move(input_packet_)] { HandleInputPacketOnWorker(std::move(packet)); });

  if (!end_of_input_stream_) {
    // Request the next packet, so it will be ready when we need it.
    RequestInputPacket();
  }
}

void SoftwareProcessor::HandleInputPacketOnWorker(PacketPtr input) {
  FX_DCHECK(is_worker_thread());
  FX_DCHECK(input);

  bool done = false;
  bool new_input = true;

  int64_t start_time = zx::clock::get_monotonic().get();

  // We depend on |TransformPacket| behaving properly here. Specifically, it
  // should return true in just a few iterations. It will normally produce an
  // output packet and/or return true. The only exception is when the output
  // allocator is exhausted.
  while (!done) {
    PacketPtr output;
    done = TransformPacket(input, new_input, &output);

    new_input = false;

    if (output) {
      PostTaskToMainThread([this, output]() { HandleOutputPacket(output); });
    }
  }

  {
    std::lock_guard<std::mutex> locker(process_duration_mutex_);
    process_duration_.AddSample(zx::clock::get_monotonic().get() - start_time);
  }

  PostTaskToMainThread([this]() { WorkerDoneWithInputPacket(); });
}

void SoftwareProcessor::HandleOutputPacket(PacketPtr packet) {
  FX_DCHECK(is_main_thread());
  FX_DCHECK(!end_of_output_stream_);

  if (flushing_) {
    // We're flushing. Discard the packet.
    return;
  }

  switch (output_state_) {
    case OutputState::kIdle:
      FX_DCHECK(false) << "HandleOutputPacket called when idle.";
      break;
    case OutputState::kWaitingForInput:
      FX_DCHECK(false) << "HandleOutputPacket called waiting for input.";
      break;
    case OutputState::kWaitingForWorker:
      // We got the requested packet. Indicate we've satisfied the request for
      // an output packet, but the worker hasn't finished with the input packet.
      output_state_ = OutputState::kWorkerNotDone;
      break;
    case OutputState::kWorkerNotDone:
      // We got an additional output packet.
      break;
  }

  end_of_output_stream_ = packet->end_of_stream();
  PutOutputPacket(std::move(packet));
}

void SoftwareProcessor::WorkerDoneWithInputPacket() {
  FX_DCHECK(is_main_thread());

  switch (output_state_) {
    case OutputState::kIdle:
      FX_DCHECK(false) << "WorkerDoneWithInputPacket called in idle state.";
      break;

    case OutputState::kWaitingForInput:
      FX_DCHECK(false) << "WorkerDoneWithInputPacket called waiting for input.";
      break;

    case OutputState::kWaitingForWorker:
      // We didn't get the requested output packet. Behave as though we just
      // got a new request.
      output_state_ = OutputState::kIdle;
      if (!flushing_) {
        RequestOutputPacket();
      }

      break;

    case OutputState::kWorkerNotDone:
      // We got the requested output packet. Done for now.
      output_state_ = OutputState::kIdle;
      break;
  }

  if (flush_callback_) {
    PostTaskToWorkerThread([this, callback = std::move(flush_callback_)]() mutable {
      Flush();
      PostTaskToMainThread(std::move(callback));
    });
  }
}

void SoftwareProcessor::Dump(std::ostream& os) const {
  FX_DCHECK(is_main_thread());

  os << label() << fostr::Indent;
  Node::Dump(os);
  os << fostr::NewLine << "output stream type:" << output_stream_type();
  os << fostr::NewLine << "state:             ";

  switch (output_state_) {
    case OutputState::kIdle:
      os << "idle";
      break;
    case OutputState::kWaitingForInput:
      os << "waiting for input";
      break;
    case OutputState::kWaitingForWorker:
      os << "waiting for worker";
      break;
    case OutputState::kWorkerNotDone:
      os << "worker not done";
      break;
  }

  os << fostr::NewLine << "flushing:          " << flushing_;
  os << fostr::NewLine << "end of input:      " << end_of_input_stream_;
  os << fostr::NewLine << "end of output:     " << end_of_output_stream_;

  if (input_packet_) {
    os << fostr::NewLine << "input packet:      " << input_packet_;
  }

  {
    std::lock_guard<std::mutex> locker(process_duration_mutex_);
    if (process_duration_.count() != 0) {
      os << fostr::NewLine << "processs:           " << process_duration_.count();
      os << fostr::NewLine << "process durations:";
      os << fostr::Indent;
      os << fostr::NewLine << "minimum        " << AsNs(process_duration_.min());
      os << fostr::NewLine << "average        " << AsNs(process_duration_.average());
      os << fostr::NewLine << "maximum        " << AsNs(process_duration_.max());
      os << fostr::Outdent;
    }
  }

  os << fostr::Outdent;
}

}  // namespace media_player
