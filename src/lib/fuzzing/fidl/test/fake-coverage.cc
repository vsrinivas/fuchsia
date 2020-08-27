// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-coverage.h"

#include <string.h>
#include <zircon/errors.h>

#include "sanitizer-cov-proxy.h"

namespace fuzzing {

FakeCoverage::FakeCoverage() : binding_(this), vmo_(nullptr), traces_(nullptr) {
  memset(counts_, 0, sizeof(counts_));
}

FakeCoverage::~FakeCoverage() {}

fidl::InterfaceRequestHandler<Coverage> FakeCoverage::GetHandler() {
  return [this](fidl::InterfaceRequest<Coverage> request) {
    if (binding_.is_bound()) {
      binding_.Unbind();
    }
    binding_.Bind(std::move(request));
  };
}

void FakeCoverage::Configure() {
  auto proxy = SanitizerCovProxy::GetInstance();
  traces_ = proxy->traces();
  memset(traces_, 0, kMaxInstructions * sizeof(Instruction));
  vmo_ = proxy->vmo();
  vmo_->signal(kBetweenIterations | kReadableSignalA | kReadableSignalB,
               kInIteration | kWritableSignalA | kWritableSignalB);
}

void FakeCoverage::AddInline8BitCounters(Buffer inline_8bit_counters,
                                         AddInline8BitCountersCallback callback) {
  pending_.push_back(std::move(inline_8bit_counters));
  callback();
}

void FakeCoverage::AddPcTable(Buffer pcs, AddPcTableCallback callback) {
  pending_.push_back(std::move(pcs));
  callback();
}

void FakeCoverage::AddTraces(zx::vmo traces, AddTracesCallback callback) {}

bool FakeCoverage::MapPending(SharedMemory *out) {
  if (pending_.empty()) {
    return false;
  }
  Buffer buffer = std::move(pending_.front());
  pending_.pop_front();
  return out->Link(buffer.vmo, buffer.size) == ZX_OK;
}

void FakeCoverage::SendIterationComplete() {
  vmo_->signal(kInIteration, kBetweenIterations);
  vmo_->wait_one(kReadableSignalA | kReadableSignalB, zx::time::infinite(), nullptr);
  Resolve();
}

void FakeCoverage::Resolve() {
  if (vmo_->wait_one(kReadableSignalA, zx::deadline_after(zx::nsec(0)), nullptr) == ZX_OK) {
    for (size_t i = 0; i < kInstructionBufferLen; ++i) {
      counts_[traces_[i].type] += 1;
    }
    vmo_->signal(kReadableSignalA, kWritableSignalA);
  }
  if (vmo_->wait_one(kReadableSignalB, zx::deadline_after(zx::nsec(0)), nullptr) == ZX_OK) {
    for (size_t i = kInstructionBufferLen; i < kMaxInstructions; ++i) {
      counts_[traces_[i].type] += 1;
    }
    vmo_->signal(kReadableSignalB, kWritableSignalB);
  }
}

size_t FakeCoverage::Count(Instruction::Type type) {
  if (type > Instruction::kMaxValue) {
    return 0;
  }
  return counts_[type];
}

bool FakeCoverage::HasCompleted() { return counts_[Instruction::kSentinel] != 0; }

}  // namespace fuzzing
