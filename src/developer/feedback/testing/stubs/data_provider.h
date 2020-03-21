// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

//  fuchsia.feedback.DataProvider service that returns canned responses for
// fuchsia::feedback::DataProvider::GetData().
class DataProvider : public fuchsia::feedback::DataProvider {
 public:
  DataProvider(fuchsia::feedback::Attachment attachment_bundle)
      : attachment_bundle_(std::move(attachment_bundle)) {}

  // Returns a request handler for binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
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

 private:
  fuchsia::feedback::Attachment attachment_bundle_;

  std::unique_ptr<fidl::Binding<fuchsia::feedback::DataProvider>> binding_;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DATA_PROVIDER_H_
