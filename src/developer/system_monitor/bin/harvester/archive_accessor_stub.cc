// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "archive_accessor_stub.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/vmo/strings.h"

namespace harvester {

std::vector<fuchsia::diagnostics::FormattedContent> ToVmo(
    const std::vector<std::string>& json_batch) {
  std::vector<fuchsia::diagnostics::FormattedContent> json_batch_vmo;
  json_batch_vmo.reserve(json_batch.size());
  for (const auto& json_chunk : json_batch) {
    fsl::SizedVmo vmo;
    FX_CHECK(fsl::VmoFromString(json_chunk, &vmo));
    fuchsia::diagnostics::FormattedContent content;
    content.set_json(std::move(vmo).ToTransport());
    json_batch_vmo.push_back(std::move(content));
  }
  return json_batch_vmo;
}

void ArchiveAccessorStub::StreamDiagnostics(
    fuchsia::diagnostics::StreamParameters stream_parameters,
    ::fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) {
  batch_iterator_binding_ =
      std::make_unique<::fidl::Binding<fuchsia::diagnostics::BatchIterator>>(
          batch_iterator_.get(), std::move(request));
}

void BatchIteratorStub::GetNext(GetNextCallback callback) {
  callback(::fit::ok(ToVmo(*next_json_batch_++)));
}

void BatchIteratorReturnsErrorStub::GetNext(GetNextCallback callback) {
  callback(::fit::error(fuchsia::diagnostics::ReaderError::IO));
}

}  // namespace harvester
