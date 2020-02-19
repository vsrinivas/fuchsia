// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_FEEDBACK_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_FEEDBACK_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include <cstdlib>
#include <map>
#include <memory>

#include "src/lib/fxl/logging.h"

namespace feedback {

// Stub fuchsia.feedback.DataProvider service that returns canned responses for
// fuchsia::feedback::DataProvider::GetData().
class StubFeedbackDataProvider : public fuchsia::feedback::DataProvider {
 public:
  StubFeedbackDataProvider()
      : StubFeedbackDataProvider(/*annotations=*/
                                 {
                                     {"feedback.annotation.1.key", "feedback.annotation.1.value"},
                                     {"feedback.annotation.2.key", "feedback.annotation.2.value"},
                                 },
                                 "feedback.attachment.bundle.key") {}

  StubFeedbackDataProvider(const std::map<std::string, std::string>& annotations,
                           const std::string& attachment_bundle_key)
      : annotations_(annotations), attachment_bundle_key_(attachment_bundle_key) {}

  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
      total_num_bindings_++;
      binding_ = std::make_unique<fidl::Binding<fuchsia::feedback::DataProvider>>(
          this, std::move(request));
    };
  }

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
  void GetScreenshot(fuchsia::feedback::ImageEncoding encoding,
                     GetScreenshotCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  uint64_t total_num_bindings() { return total_num_bindings_; }
  bool is_bound() { return binding_->is_bound(); }
  void CloseConnection() { binding_->Close(ZX_ERR_PEER_CLOSED); }

  const std::map<std::string, std::string>& annotations() { return annotations_; }
  bool has_attachment_bundle_key() { return !attachment_bundle_key_.empty(); }
  const std::string& attachment_bundle_key() { return attachment_bundle_key_; }

 protected:
  const std::map<std::string, std::string> annotations_;
  const std::string attachment_bundle_key_;

 private:
  std::unique_ptr<fidl::Binding<fuchsia::feedback::DataProvider>> binding_;
  uint64_t total_num_bindings_ = 0;
};

class StubFeedbackDataProviderReturnsNoAnnotation : public StubFeedbackDataProvider {
 public:
  StubFeedbackDataProviderReturnsNoAnnotation()
      : StubFeedbackDataProvider(/*annotations=*/{}, "feedback.attachment.bundle.key") {}

  void GetData(GetDataCallback callback) override;
};

class StubFeedbackDataProviderReturnsNoAttachment : public StubFeedbackDataProvider {
 public:
  StubFeedbackDataProviderReturnsNoAttachment()
      : StubFeedbackDataProvider(
            /*annotations=*/
            {
                {"feedback.annotation.1.key", "feedback.annotation.1.value"},
                {"feedback.annotation.2.key", "feedback.annotation.2.value"},
            },
            /*attachment_bundle_key=*/"") {}

  void GetData(GetDataCallback callback) override;
};

class StubFeedbackDataProviderReturnsNoData : public StubFeedbackDataProvider {
 public:
  StubFeedbackDataProviderReturnsNoData()
      : StubFeedbackDataProvider(
            /*annotations=*/{},
            /*attachment_bundle_key=*/"") {}

  void GetData(GetDataCallback callback) override;
};

class StubFeedbackDataProviderNeverReturning : public StubFeedbackDataProvider {
 public:
  StubFeedbackDataProviderNeverReturning()
      : StubFeedbackDataProvider(
            /*annotations=*/{},
            /*attachment_bundle_key=*/"") {}

  void GetData(GetDataCallback callback) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_TESTS_STUB_FEEDBACK_DATA_PROVIDER_H_
