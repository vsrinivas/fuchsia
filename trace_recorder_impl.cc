// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <sstream>

#include "apps/tracing/trace_recorder_impl.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/data_pipe/strings.h"

namespace tracing {
namespace {

constexpr const char* kOpeningBracket = "[";
constexpr const char* kClosingBracket = "]";
constexpr const char* kComma = ",";

}  // namespace

TraceRecorderImpl::TraceRecorderImpl() = default;
TraceRecorderImpl::~TraceRecorderImpl() = default;

void TraceRecorderImpl::Record(const mojo::String& json) {
  FTL_CHECK(producer_handle_.is_valid()) << "Recording not started";

  if (!is_first_entry_) {
    if (!mtl::BlockingCopyFromString(kComma, producer_handle_)) {
      FTL_DLOG(WARNING) << "Failed to transmit comma";
      return;
    }
  }

  if (!mtl::BlockingCopyFromString(json.get(), producer_handle_)) {
    FTL_DLOG(WARNING) << "Failed to transmit json";
    return;
  }

  is_first_entry_ = false;
}

void TraceRecorderImpl::Start(
    mojo::ScopedDataPipeProducerHandle producer_handle) {
  FTL_CHECK(!producer_handle_.is_valid()) << "Recording in progress";

  producer_handle_ = std::move(producer_handle);
  if (!mtl::BlockingCopyFromString(kOpeningBracket, producer_handle_)) {
    FTL_DLOG(WARNING) << "Failed to transmit opening bracket";
    return;
  }
  is_first_entry_ = true;
}

void TraceRecorderImpl::Stop() {
  FTL_CHECK(producer_handle_.is_valid()) << "Recording not started";

  if (!mtl::BlockingCopyFromString(kClosingBracket, producer_handle_)) {
    FTL_DLOG(WARNING) << "Failed to transmit closing bracket";
    return;
  }
  producer_handle_.reset();
  is_first_entry_ = true;
}

}  // namespace tracing
