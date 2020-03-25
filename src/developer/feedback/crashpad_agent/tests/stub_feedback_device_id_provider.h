// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_FEEDBACK_DEVICE_ID_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_FEEDBACK_DEVICE_ID_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include <optional>

#include "src/lib/fxl/logging.h"

namespace feedback {

class StubFeedbackDeviceIdProvider : public fuchsia::feedback::DeviceIdProvider {
 public:
  StubFeedbackDeviceIdProvider(const std::string& device_id) : device_id_(device_id) {}

  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::feedback::DeviceIdProvider> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::DeviceIdProvider> request) {
      binding_ = std::make_unique<fidl::Binding<fuchsia::feedback::DeviceIdProvider>>(
          this, std::move(request));
    };
  }

  void CloseConnection();

  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

 protected:
  const std::string& device_id() { return device_id_; }

 private:
  std::string device_id_;
  std::unique_ptr<fidl::Binding<fuchsia::feedback::DeviceIdProvider>> binding_;
};

class StubFeedbackDeviceIdProviderReturnsError : public StubFeedbackDeviceIdProvider {
 public:
  StubFeedbackDeviceIdProviderReturnsError() : StubFeedbackDeviceIdProvider("") {}

 private:
  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;
};

class StubFeedbackDeviceIdProviderNeverReturns : public StubFeedbackDeviceIdProvider {
 public:
  StubFeedbackDeviceIdProviderNeverReturns(const std::string& device_id)
      : StubFeedbackDeviceIdProvider(device_id) {}

 private:
  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;
};

class StubFeedbackDeviceIdProviderExpectsOneCall : public StubFeedbackDeviceIdProvider {
 public:
  StubFeedbackDeviceIdProviderExpectsOneCall(const std::string& device_id)
      : StubFeedbackDeviceIdProvider(device_id) {}

  ~StubFeedbackDeviceIdProviderExpectsOneCall();

 private:
  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

  bool is_first_ = true;
};

class StubFeedbackDeviceIdProviderClosesFirstConnection : public StubFeedbackDeviceIdProvider {
 public:
  StubFeedbackDeviceIdProviderClosesFirstConnection(const std::string& device_id)
      : StubFeedbackDeviceIdProvider(device_id) {}

 private:
  // |fuchsia::feedback::DeviceIdProvider|
  void GetId(GetIdCallback callback) override;

  bool is_first_ = true;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_FEEDBACK_DEVICE_ID_PROVIDER_H_
