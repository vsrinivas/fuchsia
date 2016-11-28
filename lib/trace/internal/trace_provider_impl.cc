// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/trace_provider_impl.h"

#include "apps/tracing/lib/trace/writer.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/handles/object_info.h"

namespace tracing {
namespace internal {

TraceProviderImpl::TraceProviderImpl(TraceRegistryPtr registry,
                                     const TraceSettings& settings)
    : registry_(std::move(registry)), binding_(this), weak_ptr_factory_(this) {
  TraceProviderPtr provider;
  fidl::InterfaceRequest<TraceProvider> provider_request =
      fidl::GetProxy(&provider);

  binding_.Bind(std::move(provider_request));

  std::string label = settings.provider_label;
  if (label.empty())
    label = mtl::GetCurrentProcessName();

  registry_->RegisterTraceProvider(std::move(provider),
                                   fidl::String::From(std::move(label)));
}

TraceProviderImpl::~TraceProviderImpl() {
  Stop();
}

void TraceProviderImpl::SetDumpCallback(DumpCallback callback) {
  dump_callback_ = callback;
}

void TraceProviderImpl::Start(mx::vmo buffer,
                              mx::eventpair fence,
                              ::fidl::Array<::fidl::String> categories) {
  Stop();
  pending_trace_.reset(
      new PendingTrace{std::move(buffer), std::move(fence),
                       categories.To<std::vector<std::string>>()});

  if (state_ == State::kStopped)
    StartPendingTrace();
}

void TraceProviderImpl::Stop() {
  pending_trace_.reset();

  if (state_ == State::kStarted) {
    state_ = State::kStopping;
    writer::StopTracing();
  }
}

void TraceProviderImpl::Dump(mx::socket output) {
  if (dump_callback_) {
    dump_callback_(std::make_unique<tracing::Dump>(std::move(output)));
  }
}

void TraceProviderImpl::StartPendingTrace() {
  FTL_DCHECK(pending_trace_);
  FTL_DCHECK(state_ == State::kStopped);

  auto pending_trace = std::move(pending_trace_);
  if (!writer::StartTracing(std::move(pending_trace->buffer),
                            std::move(pending_trace->fence),
                            std::move(pending_trace->enabled_categories),
                            [weak = weak_ptr_factory_.GetWeakPtr()](
                                tracing::writer::TraceDisposition disposition) {
                              FTL_VLOG(2) << "Trace finished: disposition="
                                          << ToUnderlyingType(disposition);
                              if (weak)
                                weak->FinishedTrace();
                            })) {
    FTL_VLOG(2) << "Failed to start pending trace";
    return;
  }

  state_ = State::kStarted;
}

void TraceProviderImpl::FinishedTrace() {
  FTL_DCHECK(state_ == State::kStarted || state_ == State::kStopping);

  state_ = State::kStopped;
  if (pending_trace_)
    StartPendingTrace();
}

}  // namespace internal
}  // namespace tracing
