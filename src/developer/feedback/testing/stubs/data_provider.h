// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <zircon/errors.h>

#include <cstdlib>
#include <map>
#include <memory>

#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

class DataProviderBase : public fuchsia::feedback::testing::DataProvider_TestBase {
 public:
  fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
      total_num_connections_++;
      binding_ = std::make_unique<fidl::Binding<fuchsia::feedback::DataProvider>>(
          this, std::move(request));
    };
  }

  // |fuchsia::feedback::testing::DataProvider_TestBase|
  void NotImplemented_(const std::string& name) override {
    FXL_NOTIMPLEMENTED() << name << " is not implemented";
  }

  uint64_t total_num_connections() { return total_num_connections_; }
  bool is_bound() { return binding_->is_bound(); }
  void CloseConnection() { binding_->Close(ZX_ERR_PEER_CLOSED); }

 private:
  std::unique_ptr<fidl::Binding<fuchsia::feedback::DataProvider>> binding_;
  uint64_t total_num_connections_ = 0;
};

class DataProvider : public DataProviderBase {
 public:
  DataProvider()
      : DataProvider(/*annotations=*/
                     {
                         {"feedback.annotation.1.key", "feedback.annotation.1.value"},
                         {"feedback.annotation.2.key", "feedback.annotation.2.value"},
                     },
                     "feedback.attachment.bundle.key") {}

  DataProvider(const std::map<std::string, std::string>& annotations,
               const std::string& attachment_bundle_key)
      : annotations_(annotations), attachment_bundle_key_(attachment_bundle_key) {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;

  const std::map<std::string, std::string>& annotations() { return annotations_; }
  bool has_attachment_bundle_key() { return !attachment_bundle_key_.empty(); }
  const std::string& attachment_bundle_key() { return attachment_bundle_key_; }

 protected:
  const std::map<std::string, std::string> annotations_;
  const std::string attachment_bundle_key_;
};

class DataProviderReturnsNoAnnotation : public DataProvider {
 public:
  DataProviderReturnsNoAnnotation()
      : DataProvider(/*annotations=*/{}, "feedback.attachment.bundle.key") {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
};

class DataProviderReturnsNoAttachment : public DataProvider {
 public:
  DataProviderReturnsNoAttachment()
      : DataProvider(
            /*annotations=*/
            {
                {"feedback.annotation.1.key", "feedback.annotation.1.value"},
                {"feedback.annotation.2.key", "feedback.annotation.2.value"},
            },
            /*attachment_bundle_key=*/"") {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
};

class DataProviderReturnsNoData : public DataProvider {
 public:
  DataProviderReturnsNoData()
      : DataProvider(
            /*annotations=*/{},
            /*attachment_bundle_key=*/"") {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
};

class DataProviderNeverReturning : public DataProvider {
 public:
  DataProviderNeverReturning()
      : DataProvider(
            /*annotations=*/{},
            /*attachment_bundle_key=*/"") {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
};

class DataProviderBundleAttachment : public DataProviderBase {
 public:
  DataProviderBundleAttachment(fuchsia::feedback::Attachment attachment_bundle)
      : attachment_bundle_(std::move(attachment_bundle)) {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;

 private:
  fuchsia::feedback::Attachment attachment_bundle_;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_DATA_PROVIDER_H_
