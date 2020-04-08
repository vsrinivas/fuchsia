// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_INSPECT_ARCHIVE_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_INSPECT_ARCHIVE_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/diagnostics/cpp/fidl_test_base.h>

#include "src/developer/feedback/testing/stubs/fidl_server.h"
#include "src/developer/feedback/testing/stubs/inspect_batch_iterator.h"

namespace feedback {
namespace stubs {

using InspectArchiveBase = STUB_FIDL_SERVER(fuchsia::diagnostics, Archive);

class InspectArchive : public InspectArchiveBase {
 public:
  InspectArchive() {}
  InspectArchive(std::unique_ptr<InspectBatchIteratorBase> batch_iterator)
      : batch_iterator_(std::move(batch_iterator)) {}

  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                         fuchsia::diagnostics::StreamParameters stream_parameters) override;

 private:
  std::unique_ptr<InspectBatchIteratorBase> batch_iterator_;
  std::unique_ptr<::fidl::Binding<fuchsia::diagnostics::BatchIterator>> batch_iterator_binding_;
};

class InspectArchiveClosesArchiveConnection : public InspectArchiveBase {
 public:
  // |fuchsia::diagnostics::Archive|
  STUB_METHOD_CLOSES_CONNECTION(StreamDiagnostics,
                                ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator>,
                                fuchsia::diagnostics::StreamParameters);
};

class InspectArchiveClosesIteratorConnection : public InspectArchiveBase {
 public:
  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                         fuchsia::diagnostics::StreamParameters stream_parameters) override;
};

}  // namespace stubs
}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_STUBS_INSPECT_ARCHIVE_H_
