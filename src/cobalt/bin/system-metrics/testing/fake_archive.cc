// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/testing/fake_archive.h"

#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {

// A faked implementation of ArchiveAccessor that repeatedly returns only one component with the
// given JSON value. Used for testing LogStatsFetcherImpl and ArchivistStatsFetcherImpl.
FakeArchive::FakeArchive(std::string return_value)
    : iterator_bindings_(std::make_unique<fidl::BindingSet<fuchsia::diagnostics::BatchIterator,
                                                           std::unique_ptr<FakeIterator>>>()),
      return_value_(std::move(return_value)) {}

void FakeArchive::StreamDiagnostics(
    fuchsia::diagnostics::StreamParameters params,
    fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) {
  iterator_bindings_->AddBinding(std::make_unique<FakeIterator>(return_value_), std::move(request));
}

FakeArchive::FakeIterator::FakeIterator(std::string return_value)
    : return_value_(std::move(return_value)) {}
void FakeArchive::FakeIterator::GetNext(GetNextCallback callback) {
  std::vector<fuchsia::diagnostics::FormattedContent> contents;
  if (!sent) {
    fuchsia::diagnostics::FormattedContent content;
    ZX_ASSERT(fsl::VmoFromString(return_value_, &content.json()));
    contents.emplace_back(std::move(content));
    sent = true;
  }
  callback(fpromise::ok(std::move(contents)));
}

}  // namespace cobalt
