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

class ChannelControlBase
    : public MULTI_BINDING_STUB_FIDL_SERVER(fuchsia::update::channelcontrol, ChannelControl) {
 public:
  struct Params {
    std::optional<std::string> current;
    std::optional<std::string> target;
  };

  ChannelControlBase(Params params)
      : current_(std::move(params.current)), target_(std::move(params.target)) {}
  ChannelControlBase() : ChannelControlBase(Params{std::nullopt, std::nullopt}) {}

 protected:
  const std::optional<std::string>& Current() const { return current_; }
  const std::optional<std::string>& Target() const { return target_; }

 private:
  std::optional<std::string> current_;
  std::optional<std::string> target_;
};

class ChannelControl : public ChannelControlBase {
 public:
  explicit ChannelControl(Params params) : ChannelControlBase(std::move(params)) {}

  // |fuchsia::update::channelcontrol::ChannelControl|.
  void GetCurrent(GetCurrentCallback callback) override;
  void GetTarget(GetCurrentCallback callback) override;
};

class ChannelControlReturnsEmptyChannel : public ChannelControlBase {
  // |fuchsia::update::channelcontrol::ChannelControl|.
  void GetCurrent(GetCurrentCallback callback) override;
  void GetTarget(GetTargetCallback callback) override;
};

class ChannelControlClosesConnection : public ChannelControlBase {
 public:
  // |fuchsia::update::channelcontrol::ChannelControl|.
  STUB_METHOD_CLOSES_ALL_CONNECTIONS(GetCurrent, GetCurrentCallback)
  STUB_METHOD_CLOSES_ALL_CONNECTIONS(GetTarget, GetTargetCallback)
};

class ChannelControlNeverReturns : public ChannelControlBase {
 public:
  // |fuchsia::update::channelcontrol::ChannelControl|.
  STUB_METHOD_DOES_NOT_RETURN(GetCurrent, GetCurrentCallback)
  STUB_METHOD_DOES_NOT_RETURN(GetTarget, GetTargetCallback)
};

class ChannelControlClosesFirstConnection : public ChannelControlBase {
 public:
  explicit ChannelControlClosesFirstConnection(Params params)
      : ChannelControlBase(std::move(params)) {}

  // |fuchsia::update::channelcontrol::ChannelControl|.
  void GetCurrent(GetCurrentCallback callback) override;
  void GetTarget(GetCurrentCallback callback) override;

 private:
  bool first_call_ = true;
};

class ChannelControlExpectsOneCall : public ChannelControlBase {
 public:
  explicit ChannelControlExpectsOneCall(Params params) : ChannelControlBase(std::move(params)) {}
  ~ChannelControlExpectsOneCall() override;

  // |fuchsia::update::channelcontrol::ChannelControl|.
  void GetCurrent(GetCurrentCallback callback) override;
  void GetTarget(GetCurrentCallback callback) override;

 private:
  bool first_call_ = true;
};

}  // namespace forensics::stubs

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CHANNEL_CONTROL_H_
