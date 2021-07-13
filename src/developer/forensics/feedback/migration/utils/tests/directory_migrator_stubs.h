// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_TESTS_DIRECTORY_MIGRATOR_STUBS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_TESTS_DIRECTORY_MIGRATOR_STUBS_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>

#include <optional>
#include <string>

#include "fbl/unique_fd.h"
#include "src/developer/forensics/feedback/migration/utils/file_utils.h"

namespace forensics::feedback {

template <typename Protocol>
class DirectoryMigratorStub : public Protocol {
 public:
  // How the stub should erroneously behave.
  enum class ErrorResponse { kDropConnection, kHang };

  DirectoryMigratorStub(std::optional<std::string> data_path, std::optional<std::string> cache_path,
                        std::optional<ErrorResponse> error_response = std::nullopt)
      : data_path_(std::move(data_path)),
        cache_path_(std::move(cache_path)),
        error_response_(error_response) {}

  void GetDirectories(typename Protocol::GetDirectoriesCallback callback) override {
    if (!error_response_) {
      callback(PathToInterfaceHandle(data_path_), PathToInterfaceHandle(cache_path_));
    }
  }

  fidl::InterfaceRequestHandler<Protocol> GetHandler() {
    return [this](fidl::InterfaceRequest<Protocol> request) {
      if (error_response_ == ErrorResponse::kDropConnection) {
        return;
      }

      bindings_.AddBinding(this, std::move(request));
    };
  }

 private:
  ::fidl::InterfaceHandle<fuchsia::io::Directory> PathToInterfaceHandle(
      const std::optional<std::string>& path) {
    ::fidl::InterfaceHandle<fuchsia::io::Directory> handle;
    if (!path) {
      return handle;
    }

    return IntoInterfaceHandle(fbl::unique_fd(open(path->c_str(), O_DIRECTORY | O_RDWR, 0777)));
  }

  std::optional<std::string> data_path_;
  std::optional<std::string> cache_path_;
  std::optional<ErrorResponse> error_response_;

  fidl::BindingSet<Protocol> bindings_;
};

template <typename Protocol>
class DirectoryMigratorStubClosesConnection : public Protocol {
 public:
  void GetDirectories(typename Protocol::GetDirectoriesCallback callback) override {}

  ::fidl::InterfaceRequestHandler<Protocol> GetHandler() {
    return [](fidl::InterfaceRequest<Protocol> request) { request.Close(ZX_ERR_PEER_CLOSED); };
  }
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_TESTS_DIRECTORY_MIGRATOR_STUBS_H_
