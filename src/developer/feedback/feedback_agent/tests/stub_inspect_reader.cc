// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/tests/stub_inspect_reader.h"

#include <lib/fit/result.h>
#include <zircon/errors.h>

namespace feedback {

void StubInspectReader::GetSnapshot(
    fuchsia::diagnostics::Format format,
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    GetSnapshotCallback callback) {
  batch_iterator_binding_ = std::make_unique<fidl::Binding<fuchsia::diagnostics::BatchIterator>>(
      batch_iterator_.get(), std::move(request));
  callback(fit::ok());
}

void StubInspectReaderClosesBatchIteratorConnection::GetSnapshot(
    fuchsia::diagnostics::Format format,
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    GetSnapshotCallback callback) {
  request.Close(ZX_ERR_PEER_CLOSED);
}

void StubInspectReaderNeverResponds::GetSnapshot(
    fuchsia::diagnostics::Format format,
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    GetSnapshotCallback callback) {
  // We still need to bind, otherwise it will trigger the batch iterator's error handler and not
  // simulate GetSnapshot() never responding.
  batch_iterator_binding_ = std::make_unique<fidl::Binding<fuchsia::diagnostics::BatchIterator>>(
      batch_iterator_.get(), std::move(request));
}

void StubInspectReaderReturnsError::GetSnapshot(
    fuchsia::diagnostics::Format format,
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request,
    GetSnapshotCallback callback) {
  callback(fit::error(fuchsia::diagnostics::ReaderError::IO));
}

}  // namespace feedback
