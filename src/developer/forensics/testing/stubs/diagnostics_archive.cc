// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/diagnostics_archive.h"

namespace forensics {
namespace stubs {

void DiagnosticsArchive::StreamDiagnostics(
    fuchsia::diagnostics::StreamParameters stream_parameters,
    ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) {
  batch_iterator_binding_ = std::make_unique<::fidl::Binding<fuchsia::diagnostics::BatchIterator>>(
      batch_iterator_.get(), std::move(request));
}

void DiagnosticsArchiveClosesIteratorConnection::StreamDiagnostics(
    fuchsia::diagnostics::StreamParameters stream_parameters,
    ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) {
  request.Close(ZX_ERR_PEER_CLOSED);
}

}  // namespace stubs
}  // namespace forensics
