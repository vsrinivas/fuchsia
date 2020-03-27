// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_INSPECT_ARCHIVE_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_INSPECT_ARCHIVE_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/diagnostics/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include "src/developer/feedback/testing/stubs/inspect_batch_iterator.h"
#include "src/lib/fxl/logging.h"

namespace feedback {
namespace stubs {

class InspectArchive : public fuchsia::diagnostics::testing::Archive_TestBase {
 public:
  InspectArchive() {}
  InspectArchive(std::unique_ptr<InspectBatchIteratorBase> batch_iterator)
      : batch_iterator_(std::move(batch_iterator)) {}

  fidl::InterfaceRequestHandler<fuchsia::diagnostics::Archive> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::diagnostics::Archive> request) {
      archive_binding_ =
          std::make_unique<fidl::Binding<fuchsia::diagnostics::Archive>>(this, std::move(request));
    };
  }

  void CloseConnection();

  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                         fuchsia::diagnostics::StreamParameters stream_parameters) override;

  // |fuchsia::diagnostics::testing::Archive_TestBase|
  void NotImplemented_(const std::string& name) override {
    FXL_NOTIMPLEMENTED() << name << " is not implemented";
  }

 protected:
  std::unique_ptr<fidl::Binding<fuchsia::diagnostics::Archive>> archive_binding_;
  std::unique_ptr<InspectBatchIteratorBase> batch_iterator_;
  std::unique_ptr<fidl::Binding<fuchsia::diagnostics::BatchIterator>> batch_iterator_binding_;
};

class InspectArchiveClosesArchiveConnection : public InspectArchive {
 public:
  InspectArchiveClosesArchiveConnection() {}

  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                         fuchsia::diagnostics::StreamParameters stream_parameters) override;
};

class InspectArchiveClosesIteratorConnection : public InspectArchive {
 public:
  InspectArchiveClosesIteratorConnection() {}

  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                         fuchsia::diagnostics::StreamParameters stream_parameters) override;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_INSPECT_ARCHIVE_H_
