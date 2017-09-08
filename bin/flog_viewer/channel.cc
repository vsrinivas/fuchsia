// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garent/bin/flog_viewer/channel.h"

#include "garent/bin/flog_viewer/formatting.h"

namespace flog {

// static
std::shared_ptr<Channel> Channel::Create(
    uint32_t log_id,
    uint32_t channel_id,
    uint32_t creation_entry_index,
    uint64_t subject_address,
    std::unique_ptr<ChannelHandler> handler) {
  return std::shared_ptr<Channel>(
      new Channel(log_id, channel_id, creation_entry_index, subject_address,
                  std::move(handler)));
}

// static
std::shared_ptr<Channel> Channel::CreateUnresolved(uint32_t log_id,
                                                   uint64_t subject_address) {
  return std::shared_ptr<Channel>(
      new Channel(log_id, 0, 0, subject_address, nullptr));
}

Channel::Channel(uint32_t log_id,
                 uint32_t channel_id,
                 uint32_t creation_entry_index,
                 uint64_t subject_address,
                 std::unique_ptr<ChannelHandler> handler)
    : log_id_(log_id),
      channel_id_(channel_id),
      creation_entry_index_(creation_entry_index),
      subject_address_(subject_address),
      handler_(std::move(handler)) {}

Channel::~Channel() {}

void Channel::Resolve(uint32_t channel_id,
                      uint32_t creation_entry_index,
                      std::unique_ptr<ChannelHandler> handler) {
  FTL_DCHECK(!resolved());
  channel_id_ = channel_id;
  creation_entry_index_ = creation_entry_index;
  handler_ = std::move(handler);
}

void Channel::PrintAccumulator(std::ostream& os) const {
  if (!resolved()) {
    os << "NOT RESOLVED\n";
    return;
  }

  std::shared_ptr<Accumulator> accumulator = handler()->GetAccumulator();
  if (!accumulator) {
    os << "NO ACCUMULATOR\n";
    return;
  }

  accumulator->Print(os);
}

}  // namespace flog
