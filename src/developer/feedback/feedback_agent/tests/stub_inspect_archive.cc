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

void StubInspectArchive::ReadInspect(fidl::InterfaceRequest<fuchsia::diagnostics::Reader> request,
                                     std::vector<fuchsia::diagnostics::SelectorArgument> selectors,
                                     ReadInspectCallback callback) {
  reader_binding_ = std::make_unique<fidl::Binding<fuchsia::diagnostics::Reader>>(
      reader_.get(), std::move(request));
  callback(fit::ok());
}

void StubInspectArchiveClosesArchiveConnection::ReadInspect(
    fidl::InterfaceRequest<fuchsia::diagnostics::Reader> request,
    std::vector<fuchsia::diagnostics::SelectorArgument> selectors, ReadInspectCallback callback) {
  CloseConnection();
}

void StubInspectArchiveClosesReaderConnection::ReadInspect(
    fidl::InterfaceRequest<fuchsia::diagnostics::Reader> request,
    std::vector<fuchsia::diagnostics::SelectorArgument> selectors, ReadInspectCallback callback) {
  request.Close(ZX_ERR_PEER_CLOSED);
}

void StubInspectArchiveNeverResponds::ReadInspect(
    fidl::InterfaceRequest<fuchsia::diagnostics::Reader> request,
    std::vector<fuchsia::diagnostics::SelectorArgument> selectors, ReadInspectCallback callback) {
  // We still need to bind, otherwise it will trigger the reader's error handler and not simulate
  // ReadInspect() never responding.
  reader_binding_ = std::make_unique<fidl::Binding<fuchsia::diagnostics::Reader>>(
      reader_.get(), std::move(request));
}

void StubInspectArchiveReturnsError::ReadInspect(
    fidl::InterfaceRequest<fuchsia::diagnostics::Reader> request,
    std::vector<fuchsia::diagnostics::SelectorArgument> selectors, ReadInspectCallback callback) {
  callback(fit::error(fuchsia::diagnostics::AccessorError::INVALID_SELECTOR));
}

}  // namespace feedback
