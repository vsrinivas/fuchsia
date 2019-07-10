// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_TESTS_STUB_CHANNEL_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_TESTS_STUB_CHANNEL_PROVIDER_H_

#include <fuchsia/update/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <string>

namespace fuchsia {
namespace feedback {

class StubUpdateInfo : public fuchsia::update::Info {
 public:
  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::update::Info> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // |fuchsia.update.info|.
  void GetChannel(GetChannelCallback callback) override;

  void set_channel(const std::string& channel) { channel_ = channel; }

 protected:
  void CloseAllConnections() { bindings_.CloseAll(); }

 private:
  fidl::BindingSet<fuchsia::update::Info> bindings_;
  std::string channel_;
};

class StubUpdateInfoClosesConnection : public StubUpdateInfo {
 public:
  void GetChannel(GetChannelCallback callback) override;
};

class StubUpdateInfoNeverReturns : public StubUpdateInfo {
 public:
  void GetChannel(GetChannelCallback callback) override;
};

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_TESTS_STUB_CHANNEL_PROVIDER_H_
