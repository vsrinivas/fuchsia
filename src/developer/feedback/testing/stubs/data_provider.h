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

#include "src/developer/feedback/testing/stubs/fidl_server.h"
#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

using DataProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::feedback, DataProvider);

class DataProvider : public DataProviderBase {
 public:
  DataProvider(const std::map<std::string, std::string>& annotations,
               const std::string& attachment_bundle_key)
      : annotations_(annotations), attachment_bundle_key_(attachment_bundle_key) {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;

 protected:
  const std::map<std::string, std::string> annotations_;
  const std::string attachment_bundle_key_;
};

class DataProviderReturnsNoAnnotation : public DataProvider {
 public:
  DataProviderReturnsNoAnnotation(const std::string& attachment_bundle_key)
      : DataProvider(/*annotations=*/{}, attachment_bundle_key) {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
};

class DataProviderReturnsNoAttachment : public DataProvider {
 public:
  DataProviderReturnsNoAttachment(const std::map<std::string, std::string>& annotations)
      : DataProvider(annotations, /*attachment_bundle_key=*/"") {}

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
};

class DataProviderReturnsNoData : public DataProviderBase {
 public:
  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;
};

class DataProviderTracksNumConnections : public DataProviderBase {
 public:
  DataProviderTracksNumConnections(size_t expected_num_connections)
      : expected_num_connections_(expected_num_connections) {}
  ~DataProviderTracksNumConnections();

  ::fidl::InterfaceRequestHandler<fuchsia::feedback::DataProvider> GetHandler() override {
    return [this](::fidl::InterfaceRequest<fuchsia::feedback::DataProvider> request) {
      ++num_connections_;
      binding().reset(
          new ::fidl::Binding<fuchsia::feedback::DataProvider>(this, std::move(request)));
    };
  }

  // |fuchsia::feedback::DataProvider|
  void GetData(GetDataCallback callback) override;

 private:
  const size_t expected_num_connections_;

  size_t num_connections_ = 0;
};

class DataProviderNeverReturning : public DataProviderBase {
 public:
  // |fuchsia::feedback::DataProvider|
  STUB_METHOD_DOES_NOT_RETURN(GetData, GetDataCallback);
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
