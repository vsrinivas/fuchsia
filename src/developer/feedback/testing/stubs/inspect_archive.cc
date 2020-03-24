// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/inspect_archive.h"

namespace feedback {
namespace stubs {

void InspectArchive::CloseConnection() {
  if (archive_binding_) {
    archive_binding_->Close(ZX_ERR_PEER_CLOSED);
  }
}

void InspectArchive::StreamDiagnostics(
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    fuchsia::diagnostics::StreamParameters stream_parameters) {
  batch_iterator_binding_ = std::make_unique<fidl::Binding<fuchsia::diagnostics::BatchIterator>>(
      batch_iterator_.get(), std::move(request));
}

void InspectArchiveClosesArchiveConnection::StreamDiagnostics(
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    fuchsia::diagnostics::StreamParameters stream_parameters) {
  CloseConnection();
}

void InspectArchiveClosesIteratorConnection::StreamDiagnostics(
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    fuchsia::diagnostics::StreamParameters stream_parameters) {
  request.Close(ZX_ERR_PEER_CLOSED);
}

}  // namespace stubs
}  // namespace feedback
