// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_CHANNEL_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_CHANNEL_PROVIDER_H_

#include <fuchsia/update/channel/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <memory>
#include <string>

namespace feedback {
namespace stubs {

class ChannelProvider : public fuchsia::update::channel::Provider {
 public:
  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::update::channel::Provider> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::update::channel::Provider> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::update::channel::Provider>>(
          this, std::move(request));
    };
  }

  // |fuchsia.update.channel.Provider|.
  void GetCurrent(GetCurrentCallback callback) override;

  void set_channel(const std::string& channel) { channel_ = channel; }

 protected:
  void CloseConnection();

 private:
  std::unique_ptr<fidl::Binding<fuchsia::update::channel::Provider>> binding_;
  std::string channel_;
};

class ChannelProviderClosesConnection : public ChannelProvider {
 public:
  void GetCurrent(GetCurrentCallback callback) override;
};

class ChannelProviderNeverReturns : public ChannelProvider {
 public:
  void GetCurrent(GetCurrentCallback callback) override;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_CHANNEL_PROVIDER_H_
