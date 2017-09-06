// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <vector>

namespace escher {
namespace impl {

class CommandBufferSequencer;

// Listener that can be registered with CommandBufferSequencer.
class CommandBufferSequencerListener {
 public:
  virtual ~CommandBufferSequencerListener() {}

  // Notify the listener that all command buffers with seq # <= sequence_number
  // have finished executing on the GPU.
  virtual void OnCommandBufferFinished(uint64_t sequence_number) = 0;

 protected:
  // Allow subclasses to register/unregister themselves.
  void Register(CommandBufferSequencer* sequencer);
  void Unregister(CommandBufferSequencer* sequencer);
};

// CommandBufferSequencer is responsible for global sequencing of CommandBuffers
// within a single Escher instance (across multiple CommandBufferPools and
// Vulkan queues).  It also tracks the highest sequence number, such that all
// CommandBuffers with equal or lower sequence number have finished execution.
class CommandBufferSequencer {
 public:
  ~CommandBufferSequencer();

  // Get the most recent sequence number generated for a CommandBuffer. All
  // future sequence numbers will be greater since sequence numbers are
  // monotonically-increasing.
  uint64_t latest_sequence_number() { return latest_sequence_number_; }

 private:
  // Allow listeners to register/unregister themselves.
  friend class CommandBufferSequencerListener;

  // Special class to only allow privileged classes to call private methods.
  friend class CommandBufferSequencerController;

  // Obtain a monotonically-increasing sequence number for a CommandBuffer that
  // is about to be obtained from a CommandBufferPool.
  uint64_t GenerateNextCommandBufferSequenceNumber();

  // Receive a notification that the CommandBuffer with the specified sequence
  // number has completed execution.
  //
  // If |sequence_number| > 1 + |last_finished_sequence_number_|, then there
  // are CommandBuffers with a lower sequence number that have not completed.
  // In this case, wait for these to complete by adding |sequence_number| to
  // |out_of_sequence_numbers_|.
  //
  // Otherwise, increment |last_finished_sequence_number_|.  Then, check whether
  // any values in |out_of_sequence_numbers_| are now "in sequence"; if so,
  // remove them and increment |last_finished_sequence_number_| accordingly.
  //
  // If either of these cases causes |last_finished_sequence_number_| to change,
  // notify all registered listeners.
  void CommandBufferFinished(uint64_t sequence_number);

  void AddListener(CommandBufferSequencerListener* listener);
  void RemoveListener(CommandBufferSequencerListener* listener);

  // The last sequence number returned by GenerateNextSequenceNumber().
  uint64_t latest_sequence_number_ = 0;
  // The highest sequence number where its command-buffer, and all command-
  // buffers for all previous sequence numbers, have finished.
  uint64_t last_finished_sequence_number_ = 0;

  // Sequence numbers of command-buffers that finished out-of-sequence.
  std::vector<uint64_t> out_of_sequence_numbers_;

  std::vector<CommandBufferSequencerListener*> listeners_;
};

// Subclassed to be able to call private methods on CommandBufferSequencer.
class CommandBufferSequencerController {
 protected:
  uint64_t GenerateNextCommandBufferSequenceNumber(CommandBufferSequencer* s) {
    return s->GenerateNextCommandBufferSequenceNumber();
  }
  void CommandBufferFinished(CommandBufferSequencer* s,
                             uint64_t sequence_number) {
    s->CommandBufferFinished(sequence_number);
  }
};

}  // namespace impl
}  // namespace escher
