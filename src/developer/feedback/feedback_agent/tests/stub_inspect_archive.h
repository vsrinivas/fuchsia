// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_ARCHIVE_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_ARCHIVE_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <string>

#include "src/developer/feedback/feedback_agent/tests/stub_inspect_batch_iterator.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

// Stub Inspect archive service to return controlled response to Archive::ReadInspect().
class StubInspectArchive : public fuchsia::diagnostics::Archive {
 public:
  StubInspectArchive() {}
  StubInspectArchive(std::unique_ptr<StubInspectBatchIteratorBase> batch_iterator)
      : batch_iterator_(std::move(batch_iterator)) {}

  // Returns a request handler for a binding to this stub service.
  fidl::InterfaceRequestHandler<fuchsia::diagnostics::Archive> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::diagnostics::Archive> request) {
      archive_binding_ =
          std::make_unique<fidl::Binding<fuchsia::diagnostics::Archive>>(this, std::move(request));
    };
  }

  void CloseConnection();

  void StreamDiagnostics(fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                         fuchsia::diagnostics::StreamParameters stream_parameters) override;

 protected:
  std::unique_ptr<fidl::Binding<fuchsia::diagnostics::Archive>> archive_binding_;
  std::unique_ptr<StubInspectBatchIteratorBase> batch_iterator_;
  std::unique_ptr<fidl::Binding<fuchsia::diagnostics::BatchIterator>> batch_iterator_binding_;
};

class StubInspectArchiveClosesArchiveConnection : public StubInspectArchive {
 public:
  StubInspectArchiveClosesArchiveConnection() {}

  void StreamDiagnostics(fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                         fuchsia::diagnostics::StreamParameters stream_parameters) override;
};

class StubInspectArchiveClosesIteratorConnection : public StubInspectArchive {
 public:
  StubInspectArchiveClosesIteratorConnection() {}

  void StreamDiagnostics(fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
                         fuchsia::diagnostics::StreamParameters stream_parameters) override;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_TESTS_STUB_INSPECT_ARCHIVE_H_
