// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-proxy.h"

#include <string.h>
#include <zircon/errors.h>

#include "remote.h"

namespace fuzzing {

FakeProxy::FakeProxy() : binding_(this), vmo_(nullptr), traces_(nullptr) {
  memset(counts_, 0, sizeof(counts_));
}

FakeProxy::~FakeProxy() {}

fidl::InterfaceRequestHandler<Proxy> FakeProxy::GetHandler() {
  return [this](fidl::InterfaceRequest<Proxy> request) {
    if (binding_.is_bound()) {
      binding_.Unbind();
    }
    binding_.Bind(std::move(request));
  };
}

void FakeProxy::Configure() {
  auto proxy = Remote::GetInstance();
  traces_ = proxy->traces();
  memset(traces_, 0, kMaxInstructions * sizeof(Instruction));
  vmo_ = proxy->vmo();
  vmo_->signal(kBetweenIterations | kReadableSignalA | kReadableSignalB,
               kInIteration | kWritableSignalA | kWritableSignalB);
}

void FakeProxy::AddInline8BitCounters(Buffer inline_8bit_counters,
                                      AddInline8BitCountersCallback callback) {
  pending_.push_back(std::move(inline_8bit_counters));
  callback();
}

void FakeProxy::AddPcTable(Buffer pcs, AddPcTableCallback callback) {
  pending_.push_back(std::move(pcs));
  callback();
}

void FakeProxy::AddTraces(zx::vmo traces, AddTracesCallback callback) {}

bool FakeProxy::MapPending(SharedMemory *out) {
  if (pending_.empty()) {
    return false;
  }
  Buffer buffer = std::move(pending_.front());
  pending_.pop_front();
  return out->Link(buffer.vmo, buffer.size) == ZX_OK;
}

void FakeProxy::SendIterationComplete() {
  vmo_->signal(kInIteration, kBetweenIterations);
  vmo_->wait_one(kReadableSignalA | kReadableSignalB, zx::time::infinite(), nullptr);
  Resolve();
}

void FakeProxy::Resolve() {
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

size_t FakeProxy::Count(Instruction::Type type) {
  if (type > Instruction::kMaxValue) {
    return 0;
  }
  return counts_[type];
}

bool FakeProxy::HasCompleted() { return counts_[Instruction::kSentinel] != 0; }

}  // namespace fuzzing
