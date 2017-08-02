// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

namespace flog {

class Channel;
class Binding;

// Abstract class for ChannelHandler callbacks.
class ChannelManager {
 public:
  // Finds a channel by subject address.
  virtual std::shared_ptr<Channel> FindChannelBySubjectAddress(
      uint32_t log_id,
      uint64_t subject_address) = 0;

  // Associates a binding koid with a binding.
  virtual void SetBindingKoid(Binding* binding, uint64_t koid) = 0;

  // Binds a channel to a binding koid.
  virtual void BindAs(std::shared_ptr<Channel> channel, uint64_t koid) = 0;
};

}  // namespace flog
