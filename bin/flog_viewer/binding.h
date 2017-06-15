// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

namespace flog {

class Channel;

// Base class for |ChildBinding| and |PeerBinding|, which are essentially
// pointers to |Channel|s that can be resolved late.
//
// When two objects are associated via a fidl binding, their respective channel
// accumulators can be associated using this class. An accumulator corresponding
// to the 'client' object (which has an |InterfacePtr| to the 'server' object)
// can use a |ChildBinding| or |PeerBinding| field to reference the channel (and
// therefore the accumulator) corresponding to the server object.
//
// In order for the binding to be resolved, the client channel handler must
// associate a koid with the |Binding| by calling |SetBindingKoid|. The koid
// refers to the server end, i.e. the 'related' koid extracted from the
// |InterfacePtr|. The channel handler for the server object must also associate
// the same koid with the corresponding channel by calling |BindAs|. The koid,
// again, refers to the server end, i.e. the koid extracted from the fidl
// |Binding| or |InterfaceRequest|.
//
// The distinction between a |ChildBinding| and a |PeerBinding| is that a
// |ChildBinding| establishes a parent/child relationship between the client
// and server, respectively. Consequently, there should be only one
// |ChildBinding| referencing a channel. Any number of |PeerBinding|s can
// reference a channel.
//
// Each binding has its own unique koid. Two bindings referencing the same
// channel, for example, have distinct koids. This implies that a channel can
// be 'bound as' any number of koids, one for each |Binding| that references
// it.
class Binding {
 public:
  Binding();

  virtual ~Binding();

  // The koid of this binding, set via |ChannelHandler::SetBindingKoid|. This
  // refers to the server end of the connection.
  uint64_t koid() const { return koid_; }

  // The channel referenced by this binding, set via |ChannelHandler::BindAs|.
  std::shared_ptr<Channel> channel() const { return channel_; }

  // Sets the koid and resets the channel.
  void SetKoid(uint64_t koid) {
    Reset();
    koid_ = koid;
  }

  // Sets the channel.
  virtual void SetChannel(std::shared_ptr<Channel> channel);

  explicit operator bool() const { return koid_ != 0; }

  // Resets the koid and channel.
  void Reset() {
    koid_ = 0;
    channel_ = nullptr;
  }

 private:
  uint64_t koid_ = 0;
  std::shared_ptr<Channel> channel_;
};

// See Binding.
class ChildBinding : public Binding {
 public:
  ChildBinding();

  ~ChildBinding() override;

  void SetChannel(std::shared_ptr<Channel> channel) override;
};

// See Binding.
class PeerBinding : public Binding {
 public:
  PeerBinding();

  ~PeerBinding() override;
};

}  // namespace flog
