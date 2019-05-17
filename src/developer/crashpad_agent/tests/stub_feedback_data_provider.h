// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <memory>
#include <vector>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace crash {

// Stub fuchsia.feedback.DataProvider service that returns canned responses for
// fuchsia::feedback::DataProvider::GetData().
class StubFeedbackDataProvider : public fuchsia::feedback::DataProvider {
 public:
  StubFeedbackDataProvider()
      : StubFeedbackDataProvider({"unused.annotation.1", "unused.annotation.2"},
                                 {"build.snapshot", "log.kernel"}) {}

  StubFeedbackDataProvider(const std::vector<std::string>& annotation_keys,
                           const std::vector<std::string>& attachment_keys)
      : annotation_keys_(annotation_keys), attachment_keys_(attachment_keys) {}

  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
  void GetScreenshot(fuchsia::feedback::ImageEncoding encoding,
                     GetScreenshotCallback callback) override {
    FXL_NOTIMPLEMENTED();
  }

  const std::vector<std::string>& attachment_keys() { return attachment_keys_; }

 protected:
  const std::vector<std::string> annotation_keys_;
  const std::vector<std::string> attachment_keys_;

 private:
  fidl::BindingSet<fuchsia::feedback::DataProvider> bindings_;
};

class StubFeedbackDataProviderReturnsNoAnnotation
    : public StubFeedbackDataProvider {
 public:
  StubFeedbackDataProviderReturnsNoAnnotation()
      : StubFeedbackDataProvider(/*annotation_keys=*/{}, /*attachment_keys=*/{
                                     "build.snapshot", "log.kernel"}) {}

  void GetData(GetDataCallback callback) override;
};

class StubFeedbackDataProviderReturnsNoAttachment
    : public StubFeedbackDataProvider {
 public:
  StubFeedbackDataProviderReturnsNoAttachment()
      : StubFeedbackDataProvider(
            /*annotation_keys=*/{"unused.annotation.1", "unused.annotation.2"},
            /*attachment_keys=*/{}) {}

  void GetData(GetDataCallback callback) override;
};

class StubFeedbackDataProviderReturnsNoData : public StubFeedbackDataProvider {
 public:
  StubFeedbackDataProviderReturnsNoData()
      : StubFeedbackDataProvider(
            /*annotation_keys=*/{},
            /*attachment_keys=*/{}) {}

  void GetData(GetDataCallback callback) override;
};

}  // namespace crash
}  // namespace fuchsia
