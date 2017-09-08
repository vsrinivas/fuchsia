// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garent/bin/flog_viewer/channel_handler.h"

namespace flog {

class Channel {
 public:
  // Creates a channel.
  static std::shared_ptr<Channel> Create(
      uint32_t log_id,
      uint32_t channel_id,
      uint32_t creation_entry_index,
      uint64_t subject_address,
      std::unique_ptr<ChannelHandler> handler);

  // Creates an unresolved channel known only by subject address.
  static std::shared_ptr<Channel> CreateUnresolved(uint32_t log_id,
                                                   uint64_t subject_address);

  ~Channel();

  // Resolves an unresolved channel.
  void Resolve(uint32_t channel_id,
               uint32_t creation_entry_index,
               std::unique_ptr<ChannelHandler> handler);

  uint32_t log_id() const { return log_id_; }
  uint32_t channel_id() const { return channel_id_; }
  uint32_t creation_entry_index() const { return creation_entry_index_; }
  uint64_t subject_address() const { return subject_address_; }
  const std::unique_ptr<ChannelHandler>& handler() const { return handler_; }

  // Determines if the channel is resolved.
  bool resolved() const { return channel_id_ != 0; }

  // Determines if the channel has an accumulator.
  bool has_accumulator() const {
    return static_cast<bool>(handler()->GetAccumulator());
  }

  // Prints the accumulator.
  void PrintAccumulator(std::ostream& os) const;

  // Determines if the channel has a parent.
  bool has_parent() const { return has_parent_; }

  // Indicates that the channel has a parent.
  void SetHasParent() { has_parent_ = true; }

 private:
  Channel(uint32_t log_id,
          uint32_t channel_id,
          uint32_t creation_entry_index,
          uint64_t subject_address,
          std::unique_ptr<ChannelHandler> handler);

  uint32_t log_id_;
  uint32_t channel_id_;
  uint32_t creation_entry_index_;
  uint64_t subject_address_;
  std::unique_ptr<ChannelHandler> handler_;
  bool has_parent_ = false;
};

}  // namespace flog
