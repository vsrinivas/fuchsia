// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <map>
#include <string>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace bugreport {

// Stub fuchsia.feedback.DataProvider service that returns canned responses for
// fuchsia::feedback::DataProvider::GetData().
class StubFeedbackDataProvider : public fuchsia::feedback::DataProvider {
 public:
  StubFeedbackDataProvider(
      const std::map<std::string, std::string>& annotations,
      const std::map<std::string, std::string>& attachments)
      : annotations_(annotations), attachments_(attachments) {}

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

 private:
  const std::map<std::string, std::string> annotations_;
  const std::map<std::string, std::string> attachments_;

  fidl::BindingSet<fuchsia::feedback::DataProvider> bindings_;
};

}  // namespace bugreport
}  // namespace fuchsia
