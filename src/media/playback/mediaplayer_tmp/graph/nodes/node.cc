// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer_tmp/graph/nodes/node.h"

#include <lib/async/cpp/task.h>
#include "src/media/playback/mediaplayer_tmp/graph/formatting.h"

namespace media_player {
namespace {

// Determines if the |PayloadManager| for the input's connection is ready.
// If so, notifies the node associated with the connected output and returns
// true. If not, returns false.
bool NotifyConnectionReady(const Input& input) {
  if (!input.connected()) {
    return false;
  }

  if (!input.payload_manager().ready()) {
    return false;
  }

  Output* output = input.mate();
  FXL_DCHECK(output);
  FXL_DCHECK(output->node());
  output->node()->NotifyOutputConnectionReady(output->index());

  return true;
}

// Determines if the |PayloadManager| for the output's connection is ready.
// If so, notifies the node associated with the connected input and returns
// true. If not, returns false.
bool NotifyConnectionReady(const Output& output) {
  if (!output.connected()) {
    return false;
  }

  Input* input = output.mate();
  FXL_DCHECK(input);

  if (!input->payload_manager().ready()) {
    return false;
  }

  FXL_DCHECK(input->node());
  input->node()->NotifyInputConnectionReady(input->index());

  return true;
}

}  // namespace

Node::Node() { update_counter_ = 0; }

Node::~Node() {}

const char* Node::label() const { return "<not labelled>"; }

void Node::ShutDown() {
  std::lock_guard<std::mutex> locker(tasks_mutex_);
  while (!tasks_.empty()) {
    tasks_.pop();
  }
}

void Node::NeedsUpdate() {
  // Atomically preincrement the update counter. If we get the value 1, that
  // means the counter was zero, and we need to post an update. If we get
  // anything else, |UpdateUntilDone| is already running. In that case, we know
  // |UpdateUntilDone| will run |Update| after the increment occurred.
  if (++update_counter_ == 1) {
    // This stage has no update pending in the task queue or running.
    PostTask([this]() { UpdateUntilDone(); });
  }
}

void Node::UpdateUntilDone() {
  while (true) {
    // Set the counter to 1. If it's still 1 after we updated, we're done.
    // Otherwise, we need to update more.
    update_counter_ = 1;

    Update();

    // Quit if the counter is still at 1, otherwise update again.
    uint32_t expected = 1;
    if (update_counter_.compare_exchange_strong(expected, 0)) {
      break;
    }
  }
}

void Node::Acquire(fit::closure callback) {
  PostTask([this, callback = std::move(callback)]() {
    {
      std::lock_guard<std::mutex> locker(tasks_mutex_);
      tasks_suspended_ = true;
    }

    callback();
  });
}

void Node::Release() {
  {
    std::lock_guard<std::mutex> locker(tasks_mutex_);
    tasks_suspended_ = false;
    if (tasks_.empty()) {
      // Don't need to run tasks.
      return;
    }
  }

  FXL_DCHECK(dispatcher_);
  async::PostTask(dispatcher_, [shared_this = shared_from_this()]() {
    shared_this->RunTasks();
  });
}

void Node::SetDispatcher(async_dispatcher_t* dispatcher) {
  FXL_DCHECK(dispatcher);
  dispatcher_ = dispatcher;
}

void Node::PostTask(fit::closure task) {
  FXL_DCHECK(task);

  {
    std::lock_guard<std::mutex> locker(tasks_mutex_);
    tasks_.push(std::move(task));
    if (tasks_.size() != 1 || tasks_suspended_) {
      // Don't need to run tasks, either because there were already tasks in
      // the queue or because task execution is suspended.
      return;
    }
  }

  FXL_DCHECK(dispatcher_);
  async::PostTask(dispatcher_, [shared_this = shared_from_this()]() {
    shared_this->RunTasks();
  });
}

void Node::PostShutdownTask(fit::closure task) {
  FXL_DCHECK(dispatcher_);
  async::PostTask(dispatcher_, [shared_this = shared_from_this(),
                                task = std::move(task)]() { task(); });
}

void Node::RunTasks() {
  tasks_mutex_.lock();

  while (!tasks_.empty() && !tasks_suspended_) {
    fit::closure task = std::move(tasks_.front());
    tasks_mutex_.unlock();
    task();
    // The closure may be keeping objects alive. Destroy it here so those
    // objects are destroyed with the mutex unlocked. It's OK to do this,
    // because this method is the only consumer of tasks from the queue, and
    // this method will not be re-entered.
    task = nullptr;
    tasks_mutex_.lock();
    tasks_.pop();
  }

  tasks_mutex_.unlock();
}

void Node::Dump(std::ostream& os) const {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  if (inputs_.size() == 1) {
    os << fostr::NewLine << "input:";
    DumpInputDetail(os, inputs_[0]);
  } else if (inputs_.size() > 1) {
    os << fostr::NewLine << "inputs:";
    int index = 0;
    for (auto& input : inputs_) {
      os << fostr::NewLine << "[" << index++ << "] ";
      DumpInputDetail(os, input);
    }
  }

  if (outputs_.size() == 1) {
    os << fostr::NewLine << "output:";
    DumpOutputDetail(os, outputs_[0]);
  } else if (outputs_.size() > 1) {
    os << fostr::NewLine << "outputs:";
    int index = 0;
    for (auto& output : outputs_) {
      os << fostr::NewLine << "[" << index++ << "] ";
      DumpOutputDetail(os, output);
    }
  }
}

void Node::DumpInputDetail(std::ostream& os, const Input& input) const {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  os << fostr::Indent;
  if (input.connected()) {
    os << fostr::NewLine << "connected to:   " << *input.mate();
  } else {
    os << fostr::NewLine << "connected to:   <nothing>";
  }

  os << fostr::NewLine << "payload config: " << input.payload_config();
  os << fostr::NewLine << "payload manager: ";
  input.payload_manager().Dump(os);

  os << fostr::NewLine << "needs packet:   " << input.needs_packet();
  os << fostr::NewLine << "packet:         " << input.packet();
  os << fostr::Outdent;
}

void Node::DumpOutputDetail(std::ostream& os, const Output& output) const {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  os << fostr::Indent;
  if (output.connected()) {
    os << fostr::NewLine << "connected to:   " << *output.mate();
  } else {
    os << fostr::NewLine << "connected to:   <nothing>";
  }

  os << fostr::NewLine << "payload config: " << output.payload_config();

  if (output.connected()) {
    os << fostr::NewLine << "needs packet:   " << output.needs_packet();
  }

  std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
  auto& packets = packets_per_output_[output.index()];
  if (!packets.empty()) {
    os << fostr::NewLine << "queued packets:" << fostr::Indent;

    for (auto& packet : packets) {
      os << fostr::NewLine << packet;
    }

    os << fostr::Outdent;
  }

  os << fostr::Outdent;
}

size_t Node::input_count() const {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  return inputs_.size();
};

Input& Node::input(size_t input_index) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(input_index < inputs_.size());
  return inputs_[input_index];
}

size_t Node::output_count() const {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  return outputs_.size();
}

Output& Node::output(size_t output_index) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(output_index < outputs_.size());
  return outputs_[output_index];
}

void Node::NotifyInputConnectionReady(size_t index) {
  FXL_DCHECK(index < inputs_.size());

  PostTask([this, index]() {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    OnInputConnectionReady(index);
  });
}

void Node::NotifyOutputConnectionReady(size_t index) {
  FXL_DCHECK(index < outputs_.size());

  PostTask([this, index]() {
    FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
    OnOutputConnectionReady(index);
  });
}

void Node::Update() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  for (auto& input : inputs_) {
    if (input.packet()) {
      PutInputPacket(input.TakePacket(false), input.index());
    }
  }

  bool request_packet = false;

  for (auto& output : outputs_) {
    if (!output.connected()) {
      continue;
    }

    PacketPtr packet_to_supply;
    if (MaybeTakePacketForOutput(output, &packet_to_supply)) {
      request_packet = true;
    }

    if (packet_to_supply) {
      output.SupplyPacket(std::move(packet_to_supply));
    }
  }

  if (request_packet) {
    RequestOutputPacket();
  }
}

bool Node::MaybeTakePacketForOutput(const Output& output,
                                    PacketPtr* packet_out) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(packet_out);

  if (!output.needs_packet()) {
    return false;
  }

  bool request_packet = false;

  std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
  std::deque<PacketPtr>& packets = packets_per_output_[output.index()];

  if (packets.empty()) {
    // The output needs a packet and has no packets queued. Request another
    // packet so we can meet the demand.
    request_packet = true;
  } else {
    // The output has demand and packets queued.
    *packet_out = std::move(packets.front());
    packets.pop_front();
  }

  return request_packet;
}

void Node::FlushInputExternal(size_t input_index, bool hold_frame,
                              fit::closure callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(input_index < inputs_.size());

  inputs_[input_index].Flush();

  FlushInput(hold_frame, input_index,
             [this, callback = std::move(callback)]() mutable {
               PostTask(std::move(callback));
             });
}

void Node::FlushOutputExternal(size_t output_index, fit::closure callback) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FXL_DCHECK(output_index < outputs_.size());

  FlushOutput(output_index,
              [this, output_index, callback = std::move(callback)]() mutable {
                {
                  std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
                  auto& packets = packets_per_output_[output_index];
                  while (!packets.empty()) {
                    packets.pop_front();
                  }
                }

                PostTask(std::move(callback));
              });
}

void Node::ConfigureInputDeferred(size_t input_index) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  EnsureInput(input_index);
}

bool Node::ConfigureInputToUseLocalMemory(uint64_t max_aggregate_payload_size,
                                          uint32_t max_payload_count,
                                          size_t input_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(max_aggregate_payload_size != 0 || max_payload_count != 0);

  EnsureInput(input_index);
  Input& input = inputs_[input_index];

  PayloadConfig& config = input.payload_config();
  config.mode_ = PayloadMode::kUsesLocalMemory;
  config.max_aggregate_payload_size_ = max_aggregate_payload_size;
  config.max_payload_count_ = max_payload_count;
  config.max_payload_size_ = 0;
  config.vmo_allocation_ = VmoAllocation::kNotApplicable;
  config.physically_contiguous_ = false;

  input.payload_manager().ApplyInputConfiguration(config, zx::handle(),
                                                  nullptr);

  return NotifyConnectionReady(input);
}

bool Node::ConfigureInputToUseVmos(
    uint64_t max_aggregate_payload_size, uint32_t max_payload_count,
    uint64_t max_payload_size, VmoAllocation vmo_allocation,
    bool physically_contiguous, zx::handle bti_handle,
    AllocateCallback allocate_callback, size_t input_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(max_aggregate_payload_size != 0 || max_payload_count != 0);
  FXL_DCHECK(physically_contiguous == bti_handle.is_valid());

  EnsureInput(input_index);
  Input& input = inputs_[input_index];

  PayloadConfig& config = input.payload_config();
  config.mode_ = PayloadMode::kUsesVmos;
  config.max_aggregate_payload_size_ = max_aggregate_payload_size;
  config.max_payload_count_ = max_payload_count;
  config.max_payload_size_ = max_payload_size;
  config.vmo_allocation_ = vmo_allocation;
  config.physically_contiguous_ = physically_contiguous;

  input.payload_manager().ApplyInputConfiguration(config, std::move(bti_handle),
                                                  std::move(allocate_callback));

  return NotifyConnectionReady(input);
}

bool Node::ConfigureInputToProvideVmos(VmoAllocation vmo_allocation,
                                       bool physically_contiguous,
                                       AllocateCallback allocate_callback,
                                       size_t input_index) {
  // This method runs on an arbitrary thread.
  EnsureInput(input_index);
  Input& input = inputs_[input_index];

  PayloadConfig& config = input.payload_config();
  config.mode_ = PayloadMode::kProvidesVmos;
  config.max_aggregate_payload_size_ = 0;
  config.max_payload_count_ = 0;
  config.max_payload_size_ = 0;
  config.vmo_allocation_ = vmo_allocation;
  config.physically_contiguous_ = physically_contiguous;

  input.payload_manager().ApplyInputConfiguration(config, zx::handle(),
                                                  std::move(allocate_callback));

  return NotifyConnectionReady(input);
}

bool Node::InputConnectionReady(size_t input_index) const {
  FXL_DCHECK(input_index < inputs_.size());

  return inputs_[input_index].payload_manager().ready();
}

const PayloadVmos& Node::UseInputVmos(size_t input_index) const {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(input_index < inputs_.size());
  const Input& input = inputs_[input_index];

  FXL_DCHECK(input.payload_config().mode_ == PayloadMode::kUsesVmos ||
             input.payload_config().mode_ == PayloadMode::kProvidesVmos);
  FXL_DCHECK(input.payload_manager().ready());

  return input.payload_manager().input_vmos();
}

PayloadVmoProvision& Node::ProvideInputVmos(size_t input_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(input_index < inputs_.size());
  Input& input = inputs_[input_index];

  FXL_DCHECK(input.payload_config().mode_ == PayloadMode::kProvidesVmos);
  FXL_DCHECK(input.payload_manager().ready());

  return input.payload_manager().input_external_vmos();
}

void Node::RequestInputPacket(size_t input_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(input_index < inputs_.size());

  inputs_[input_index].RequestPacket();
}

void Node::ConfigureOutputDeferred(size_t output_index) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  EnsureOutput(output_index);
}

bool Node::ConfigureOutputToUseLocalMemory(uint64_t max_aggregate_payload_size,
                                           uint32_t max_payload_count,
                                           uint64_t max_payload_size,
                                           size_t output_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(max_aggregate_payload_size != 0 ||
             (max_payload_count != 0 && max_payload_size != 0));

  EnsureOutput(output_index);
  Output& output = outputs_[output_index];

  PayloadConfig& config = output.payload_config();
  config.mode_ = PayloadMode::kUsesLocalMemory;
  config.max_aggregate_payload_size_ = max_aggregate_payload_size;
  config.max_payload_count_ = max_payload_count;
  config.max_payload_size_ = max_payload_size;
  config.vmo_allocation_ = VmoAllocation::kNotApplicable;
  config.physically_contiguous_ = false;

  if (output.connected()) {
    output.mate()->payload_manager().ApplyOutputConfiguration(config,
                                                              zx::handle());
  }

  return NotifyConnectionReady(output);
}

bool Node::ConfigureOutputToProvideLocalMemory(size_t output_index) {
  // This method runs on an arbitrary thread.
  EnsureOutput(output_index);
  Output& output = outputs_[output_index];

  PayloadConfig& config = output.payload_config();
  config.mode_ = PayloadMode::kProvidesLocalMemory;
  config.max_aggregate_payload_size_ = 0;
  config.max_payload_count_ = 0;
  config.max_payload_size_ = 0;
  config.vmo_allocation_ = VmoAllocation::kNotApplicable;
  config.physically_contiguous_ = false;

  if (output.connected()) {
    output.mate()->payload_manager().ApplyOutputConfiguration(config,
                                                              zx::handle());
  }

  return NotifyConnectionReady(output);
}

bool Node::ConfigureOutputToUseVmos(
    uint64_t max_aggregate_payload_size, uint32_t max_payload_count,
    uint64_t max_payload_size, VmoAllocation vmo_allocation,
    bool physically_contiguous, zx::handle bti_handle, size_t output_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(max_aggregate_payload_size != 0 ||
             (max_payload_count != 0 && max_payload_size != 0));
  FXL_DCHECK(physically_contiguous == bti_handle.is_valid());

  EnsureOutput(output_index);
  Output& output = outputs_[output_index];

  PayloadConfig& config = output.payload_config();
  config.mode_ = PayloadMode::kUsesVmos;
  config.max_aggregate_payload_size_ = max_aggregate_payload_size;
  config.max_payload_count_ = max_payload_count;
  config.max_payload_size_ = max_payload_size;
  config.vmo_allocation_ = vmo_allocation;
  config.physically_contiguous_ = physically_contiguous;

  if (output.connected()) {
    output.mate()->payload_manager().ApplyOutputConfiguration(
        config, std::move(bti_handle));
  } else {
    // TODO: stash the bti handle
  }

  return NotifyConnectionReady(output);
}

bool Node::ConfigureOutputToProvideVmos(VmoAllocation vmo_allocation,
                                        bool physically_contiguous,
                                        size_t output_index) {
  // This method runs on an arbitrary thread.
  EnsureOutput(output_index);
  Output& output = outputs_[output_index];

  PayloadConfig& config = output.payload_config();
  config.mode_ = PayloadMode::kProvidesVmos;
  config.max_aggregate_payload_size_ = 0;
  config.max_payload_count_ = 0;
  config.max_payload_size_ = 0;
  config.vmo_allocation_ = vmo_allocation;
  config.physically_contiguous_ = physically_contiguous;

  if (output.connected()) {
    output.mate()->payload_manager().ApplyOutputConfiguration(config,
                                                              zx::handle());
  }

  return NotifyConnectionReady(output);
}

bool Node::OutputConnectionReady(size_t output_index) const {
  FXL_DCHECK(output_index < outputs_.size());

  return outputs_[output_index].mate()->payload_manager().ready();
}

fbl::RefPtr<PayloadBuffer> Node::AllocatePayloadBuffer(uint64_t size,
                                                       size_t output_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(output_index < outputs_.size());
  Output& output = outputs_[output_index];

  FXL_DCHECK(output.payload_config().mode_ != PayloadMode::kNotConfigured);
  FXL_DCHECK(output.connected());
  FXL_DCHECK(output.mate()->payload_manager().ready());

  return output.mate()->payload_manager().AllocatePayloadBufferForOutput(size);
}

const PayloadVmos& Node::UseOutputVmos(size_t output_index) const {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(output_index < outputs_.size());
  const Output& output = outputs_[output_index];

  FXL_DCHECK(output.payload_config().mode_ == PayloadMode::kUsesVmos ||
             output.payload_config().mode_ == PayloadMode::kProvidesVmos);
  FXL_DCHECK(output.connected());
  FXL_DCHECK(output.mate()->payload_manager().ready());

  return output.mate()->payload_manager().output_vmos();
}

PayloadVmoProvision& Node::ProvideOutputVmos(size_t output_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(output_index < outputs_.size());
  Output& output = outputs_[output_index];

  FXL_DCHECK(output.payload_config().mode_ == PayloadMode::kProvidesVmos);
  FXL_DCHECK(output.connected());
  FXL_DCHECK(output.mate()->payload_manager().ready());

  return output.mate()->payload_manager().output_external_vmos();
}

void Node::PutOutputPacket(PacketPtr packet, size_t output_index) {
  // This method runs on an arbitrary thread.
  FXL_DCHECK(packet);
  FXL_DCHECK(output_index < outputs_.size());

  // Queue the packet if the output is connected, otherwise discard the
  // packet.
  if (outputs_[output_index].connected()) {
    std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
    packets_per_output_[output_index].push_back(std::move(packet));
  }

  NeedsUpdate();
}

void Node::EnsureInput(size_t input_index) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  while (inputs_.size() <= input_index) {
    inputs_.emplace_back(this, inputs_.size());  // node, index
  }
}

void Node::EnsureOutput(size_t output_index) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  while (outputs_.size() <= output_index) {
    outputs_.emplace_back(this, outputs_.size());  // node, index
  }

  std::lock_guard<std::mutex> locker(packets_per_output_mutex_);
  packets_per_output_.resize(output_index + 1);
}

}  // namespace media_player
