// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake-inline-8bit-counters.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>
#include <zircon/errors.h>

#include <algorithm>

#include "sanitizer-cov-proxy.h"

namespace fuzzing {
namespace {

const size_t kLength = 16;

}  // namespace

FakeInline8BitCounters::~FakeInline8BitCounters() {
  if (resetter_.joinable()) {
    resetter_.join();
  }
}

/* static */ FakeInline8BitCounters *FakeInline8BitCounters::GetInstance() {
  static FakeInline8BitCounters instance;
  return &instance;
}

FakeInline8BitCounters::FakeInline8BitCounters() {}

zx_status_t FakeInline8BitCounters::WriteImpl(const uint8_t *data, size_t size) {
  char buf[256];
  sprintf(buf, "%p", data);
  if (!data_) {
    return ZX_ERR_BAD_STATE;
  }
  if (data && size != 0) {
    memcpy(data_.get(), data, std::min(size, kLength));
  }
  return ZX_OK;
}

uint8_t FakeInline8BitCounters::AtImpl(size_t offset) {
  char buf[256];
  sprintf(buf, "%02x", data_[offset]);
  return data_ ? data_[offset] : 255;
}

zx_status_t FakeInline8BitCounters::ResetImpl(zx_duration_t timeout) {
  // It'd be nice to use the real __sanitizer_cov_* symbols, but since this test runs in a single
  // process those symbols are already used by the Coverage service to record trace data with the
  // FakeSanitizerCovProxy. Use the static methods of the (real) SanitizerCovProxy class instead.
  if (!resetter_.joinable()) {
    sync_completion_reset(&sync_);
    data_.reset(new uint8_t[kLength]);
    resetter_ = std::thread([this]() {
      SanitizerCovProxy::Init8BitCounters(data_.get(), data_.get() + kLength);
      sync_completion_signal(&sync_);
    });
  }
  zx_status_t status;
  if ((status = sync_completion_wait(&sync_, timeout)) == ZX_ERR_TIMED_OUT) {
    return false;
  }
  ZX_ASSERT(status == ZX_OK);
  resetter_.join();
  return true;
}

}  // namespace fuzzing
