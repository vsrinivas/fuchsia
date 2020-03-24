// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/testing/stubs/inspect_batch_iterator.h"

#include <lib/fit/result.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {
namespace stubs {
namespace {

std::vector<fuchsia::diagnostics::FormattedContent> ToVmo(
    const std::vector<std::string>& json_batch) {
  std::vector<fuchsia::diagnostics::FormattedContent> json_batch_vmo;
  json_batch_vmo.reserve(json_batch.size());
  for (const auto& json_chunk : json_batch) {
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(json_chunk, &vmo));
    fuchsia::diagnostics::FormattedContent content;
    content.set_json(std::move(vmo).ToTransport());
    json_batch_vmo.push_back(std::move(content));
  }
  return json_batch_vmo;
}

}  // namespace

InspectBatchIterator::~InspectBatchIterator() {
  FXL_CHECK(!ExpectCall()) << fxl::StringPrintf(
      "Expected %ld more calls to GetNext() (%ld/%lu calls made)",
      std::distance(next_json_batch_, json_batches_.cend()),
      std::distance(json_batches_.cbegin(), next_json_batch_), json_batches_.size());
}

void InspectBatchIterator::GetNext(GetNextCallback callback) {
  FXL_CHECK(ExpectCall()) << fxl::StringPrintf(
      "No more calls to GetNext() expected (%lu/%lu calls made)",
      std::distance(json_batches_.cbegin(), next_json_batch_), json_batches_.size());

  callback(fit::ok(ToVmo(*next_json_batch_++)));
}

void InspectBatchIteratorNeverRespondsAfterOneBatch::GetNext(GetNextCallback callback) {
  if (has_returned_batch_) {
    return;
  }

  callback(fit::ok(ToVmo(json_batch_)));
  has_returned_batch_ = true;
}

void InspectBatchIteratorNeverResponds::GetNext(GetNextCallback callback) {}

void InspectBatchIteratorReturnsError::GetNext(GetNextCallback callback) {
  callback(fit::error(fuchsia::diagnostics::ReaderError::IO));
}

}  // namespace stubs
}  // namespace feedback
