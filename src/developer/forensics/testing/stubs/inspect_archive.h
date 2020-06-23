// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_INSPECT_ARCHIVE_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_INSPECT_ARCHIVE_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/diagnostics/cpp/fidl_test_base.h>

#include "src/developer/forensics/testing/stubs/fidl_server.h"
#include "src/developer/forensics/testing/stubs/inspect_batch_iterator.h"

namespace forensics {
namespace stubs {

using InspectArchiveBase = SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::diagnostics, ArchiveAccessor);

class InspectArchive : public InspectArchiveBase {
 public:
  InspectArchive() {}
  InspectArchive(std::unique_ptr<InspectBatchIteratorBase> batch_iterator)
      : batch_iterator_(std::move(batch_iterator)) {}

  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(
      fuchsia::diagnostics::StreamParameters stream_parameters,
      ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) override;

 private:
  std::unique_ptr<InspectBatchIteratorBase> batch_iterator_;
  std::unique_ptr<::fidl::Binding<fuchsia::diagnostics::BatchIterator>> batch_iterator_binding_;
};

class InspectArchiveClosesArchiveConnection : public InspectArchiveBase {
 public:
  // |fuchsia::diagnostics::ArchiveAccessor|
  STUB_METHOD_CLOSES_CONNECTION(StreamDiagnostics, fuchsia::diagnostics::StreamParameters,
                                ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator>);
};

class InspectArchiveClosesIteratorConnection : public InspectArchiveBase {
 public:
  // |fuchsia::diagnostics::Archive|
  void StreamDiagnostics(
      fuchsia::diagnostics::StreamParameters stream_parameters,
      ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) override;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_INSPECT_ARCHIVE_H_
