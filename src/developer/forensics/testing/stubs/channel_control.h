// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CHANNEL_CONTROL_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CHANNEL_CONTROL_H_

#include <fuchsia/update/channelcontrol/cpp/fidl.h>
#include <fuchsia/update/channelcontrol/cpp/fidl_test_base.h>

#include <memory>
#include <string>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics::stubs {

using ChannelControlBase = MULTI_BINDING_STUB_FIDL_SERVER(fuchsia::update::channelcontrol,
                                                          ChannelControl);

class ChannelControl : public ChannelControlBase {
 public:
  explicit ChannelControl(std::string channel) : channel_(std::move(channel)) {}

  // |fuchsia::update::channelcontrol::ChannelControl|.
  void GetCurrent(GetCurrentCallback callback) override;

 private:
  std::string channel_;
};

class ChannelControlReturnsEmptyChannel : public ChannelControlBase {
  // |fuchsia::update::channelcontrol::ChannelControl|.
  void GetCurrent(GetCurrentCallback callback) override;
};

class ChannelControlClosesConnection : public ChannelControlBase {
 public:
  // |fuchsia::update::channelcontrol::ChannelControl|.
  STUB_METHOD_CLOSES_ALL_CONNECTIONS(GetCurrent, GetCurrentCallback);
};

class ChannelControlNeverReturns : public ChannelControlBase {
 public:
  // |fuchsia::update::channelcontrol::ChannelControl|.
  STUB_METHOD_DOES_NOT_RETURN(GetCurrent, GetCurrentCallback);
};

class ChannelControlClosesFirstConnection : public ChannelControlBase {
 public:
  explicit ChannelControlClosesFirstConnection(std::string channel)
      : channel_(std::move(channel)) {}

  // |fuchsia::update::channelcontrol::ChannelControl|.
  void GetCurrent(GetCurrentCallback callback) override;

 private:
  bool first_call_ = true;
  std::string channel_;
};

class ChannelControlExpectsOneCall : public ChannelControlBase {
 public:
  explicit ChannelControlExpectsOneCall(std::string channel) : channel_(std::move(channel)) {}
  ~ChannelControlExpectsOneCall() override;

  // |fuchsia::update::channelcontrol::ChannelControl|.
  void GetCurrent(GetCurrentCallback callback) override;

 private:
  bool first_call_ = true;
  std::string channel_;
};

}  // namespace forensics::stubs

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CHANNEL_CONTROL_H_
