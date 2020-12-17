// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DIAGNOSTICS_ARCHIVE_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DIAGNOSTICS_ARCHIVE_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/diagnostics/cpp/fidl_test_base.h>

#include "src/developer/forensics/testing/stubs/diagnostics_batch_iterator.h"
#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

using DiagnosticsArchiveBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::diagnostics,
                                                               ArchiveAccessor);

class DiagnosticsArchive : public DiagnosticsArchiveBase {
 public:
  DiagnosticsArchive() {}
  DiagnosticsArchive(std::unique_ptr<DiagnosticsBatchIteratorBase> batch_iterator)
      : batch_iterator_(std::move(batch_iterator)) {}

  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(
      fuchsia::diagnostics::StreamParameters stream_parameters,
      ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) override;

 private:
  std::unique_ptr<DiagnosticsBatchIteratorBase> batch_iterator_;
  std::unique_ptr<::fidl::Binding<fuchsia::diagnostics::BatchIterator>> batch_iterator_binding_;
};

class DiagnosticsArchiveCaptureParameters : public DiagnosticsArchiveBase {
 public:
  DiagnosticsArchiveCaptureParameters() = default;
  explicit DiagnosticsArchiveCaptureParameters(fuchsia::diagnostics::StreamParameters* parameters)
      : parameters_(parameters) {}

  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(
      fuchsia::diagnostics::StreamParameters stream_parameters,
      ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) override {
    *parameters_ = std::move(stream_parameters);
  }

 private:
  // Not owned
  fuchsia::diagnostics::StreamParameters* parameters_;
};

class DiagnosticsArchiveClosesArchiveConnection : public DiagnosticsArchiveBase {
 public:
  // |fuchsia::diagnostics::ArchiveAccessor|
  STUB_METHOD_CLOSES_CONNECTION(StreamDiagnostics, fuchsia::diagnostics::StreamParameters,
                                ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator>);
};

class DiagnosticsArchiveClosesIteratorConnection : public DiagnosticsArchiveBase {
 public:
  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(
      fuchsia::diagnostics::StreamParameters stream_parameters,
      ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) override;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_DIAGNOSTICS_ARCHIVE_H_
