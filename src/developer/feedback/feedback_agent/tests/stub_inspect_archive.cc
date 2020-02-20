// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/tests/stub_inspect_archive.h"

#include <lib/fit/result.h>

namespace feedback {

void StubInspectArchive::CloseConnection() {
  if (archive_binding_) {
    archive_binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void StubInspectArchive::StreamDiagnostics(
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    fuchsia::diagnostics::StreamParameters stream_parameters) {
  batch_iterator_binding_ = std::make_unique<fidl::Binding<fuchsia::diagnostics::BatchIterator>>(
      batch_iterator_.get(), std::move(request));
}

void StubInspectArchiveClosesArchiveConnection::StreamDiagnostics(
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    fuchsia::diagnostics::StreamParameters stream_parameters) {
  CloseConnection();
}

void StubInspectArchiveClosesIteratorConnection::StreamDiagnostics(
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    fuchsia::diagnostics::StreamParameters stream_parameters) {
  request.Close(ZX_ERR_PEER_CLOSED);
}

}  // namespace feedback
