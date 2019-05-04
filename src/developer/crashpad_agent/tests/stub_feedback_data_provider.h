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

  // Stub injection methods.
  void set_annotation_keys(const std::vector<std::string>& annotation_keys) {
    next_annotation_keys_ =
        std::make_unique<std::vector<std::string>>(annotation_keys);
  }
  void set_attachment_keys(const std::vector<std::string>& attachment_keys) {
    next_attachment_keys_ =
        std::make_unique<std::vector<std::string>>(attachment_keys);
  }
  void reset_annotation_keys() { next_annotation_keys_.reset(); }
  void reset_attachment_keys() { next_attachment_keys_.reset(); }

 private:
  fidl::BindingSet<fuchsia::feedback::DataProvider> bindings_;
  std::unique_ptr<std::vector<std::string>> next_annotation_keys_;
  std::unique_ptr<std::vector<std::string>> next_attachment_keys_;
};

}  // namespace crash
}  // namespace fuchsia
