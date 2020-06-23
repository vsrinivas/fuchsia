// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CHANNEL_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CHANNEL_PROVIDER_H_

#include <fuchsia/update/channel/cpp/fidl.h>
#include <fuchsia/update/channel/cpp/fidl_test_base.h>

#include <memory>
#include <string>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using ChannelProviderBase = MULTI_BINDING_STUB_FIDL_SERVER(fuchsia::update::channel, Provider);

class ChannelProvider : public ChannelProviderBase {
 public:
  ChannelProvider(const std::string channel) : channel_(channel) {}

  // |fuchsia::update::channel::Provider|.
  void GetCurrent(GetCurrentCallback callback) override;

 private:
  std::string channel_;
};

class ChannelProviderReturnsEmptyChannel : public ChannelProviderBase {
  // |fuchsia::update::channel::Provider|.
  void GetCurrent(GetCurrentCallback callback) override;
};

class ChannelProviderClosesConnection : public ChannelProviderBase {
 public:
  // |fuchsia::update::channel::Provider|.
  STUB_METHOD_CLOSES_ALL_CONNECTIONS(GetCurrent, GetCurrentCallback);
};

class ChannelProviderNeverReturns : public ChannelProviderBase {
 public:
  // |fuchsia::update::channel::Provider|.
  STUB_METHOD_DOES_NOT_RETURN(GetCurrent, GetCurrentCallback);
};

class ChannelProviderClosesFirstConnection : public ChannelProviderBase {
 public:
  ChannelProviderClosesFirstConnection(const std::string channel) : channel_(channel) {}

  // |fuchsia::update::channel::Provider|.
  void GetCurrent(GetCurrentCallback callback) override;

 private:
  bool first_call_ = true;
  std::string channel_;
};

class ChannelProviderExpectsOneCall : public ChannelProviderBase {
 public:
  ChannelProviderExpectsOneCall(const std::string channel) : channel_(channel) {}
  ~ChannelProviderExpectsOneCall();

  // |fuchsia::update::channel::Provider|.
  void GetCurrent(GetCurrentCallback callback) override;

 private:
  bool first_call_ = true;
  std::string channel_;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_CHANNEL_PROVIDER_H_
