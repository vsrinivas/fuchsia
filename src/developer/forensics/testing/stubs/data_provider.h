// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DATA_PROVIDER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DATA_PROVIDER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <cstdlib>
#include <map>
#include <memory>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using DataProviderBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::feedback, DataProvider);

class DataProvider : public DataProviderBase {
 public:
  DataProvider(const std::map<std::string, std::string>& annotations,
               const std::string& snapshot_key)
      : annotations_(annotations), snapshot_key_(snapshot_key) {}

  // |fuchsia::feedback::DataProvider|
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;

 protected:
  const std::map<std::string, std::string> annotations_;
  const std::string snapshot_key_;
};

class DataProviderReturnsNoAnnotation : public DataProvider {
 public:
  DataProviderReturnsNoAnnotation(const std::string& snapshot_key)
      : DataProvider(/*annotations=*/{}, snapshot_key) {}

  // |fuchsia::feedback::DataProvider|
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;
};

class DataProviderReturnsNoAttachment : public DataProvider {
 public:
  DataProviderReturnsNoAttachment(const std::map<std::string, std::string>& annotations)
      : DataProvider(annotations, /*snapshot_key=*/"") {}

  // |fuchsia::feedback::DataProvider|
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;
};

class DataProviderReturnsEmptySnapshot : public DataProviderBase {
 public:
  // |fuchsia::feedback::DataProvider|
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;
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
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;

 private:
  const size_t expected_num_connections_;

  size_t num_connections_ = 0;
};

class DataProviderNeverReturning : public DataProviderBase {
 public:
  // |fuchsia::feedback::DataProvider|
  STUB_METHOD_DOES_NOT_RETURN(GetSnapshot, fuchsia::feedback::GetSnapshotParameters,
                              GetSnapshotCallback);
};

class DataProviderSnapshotOnly : public DataProviderBase {
 public:
  DataProviderSnapshotOnly(fuchsia::feedback::Attachment snapshot)
      : snapshot_(std::move(snapshot)) {}

  // |fuchsia::feedback::DataProvider|
  void GetSnapshot(fuchsia::feedback::GetSnapshotParameters params,
                   GetSnapshotCallback callback) override;

 private:
  fuchsia::feedback::Attachment snapshot_;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DATA_PROVIDER_H_
