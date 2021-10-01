// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/corpus.h"

#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

namespace fuzzing {

// Public methods

Corpus::Corpus() { inputs_.emplace_back(Input()); }

Corpus& Corpus::operator=(Corpus&& other) noexcept {
  options_ = other.options_;
  other.options_ = nullptr;
  prng_ = other.prng_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::lock_guard<std::mutex> other_lock(other.mutex_);
    inputs_ = std::move(other.inputs_);
    other.inputs_.emplace_back(Input());
    total_size_ = other.total_size_;
    other.total_size_ = 0;
  }
  return *this;
}

size_t Corpus::num_inputs() {
  std::lock_guard<std::mutex> lock(mutex_);
  return inputs_.size();
}

size_t Corpus::total_size() {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_size_;
}

void Corpus::AddDefaults(Options* options) {
  if (!options->has_seed()) {
    options->set_seed(kDefaultSeed);
  }
  if (!options->has_max_input_size()) {
    options->set_max_input_size(kDefaultMaxInputSize);
  }
}

void Corpus::Configure(const std::shared_ptr<Options>& options) {
  options_ = options;
  prng_.seed(options_->seed());
}

zx_status_t Corpus::Add(Input input) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (input.size() == 0) {
    // Empty input is already implicitly included.
    return ZX_OK;
  }
  FX_DCHECK(options_);
  if (input.size() > options_->max_input_size()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  total_size_ += input.size();
  inputs_.push_back(std::move(input));
  return ZX_OK;
}

Input* Corpus::At(size_t offset) {
  std::lock_guard<std::mutex> lock(mutex_);
  return offset < inputs_.size() ? &inputs_[offset] : nullptr;
}

Input* Corpus::Pick() {
  std::lock_guard<std::mutex> lock(mutex_);
  // Use rejection sampling to get uniform distribution.
  // NOLINTNEXTLINE(google-runtime-int)
  static_assert(sizeof(unsigned long long) * 8 == 64);
  uint64_t size = inputs_.size();
  FX_DCHECK(size > 0);
  auto shift = 64 - __builtin_clzll(size);
  FX_DCHECK(size < 64);
  auto mod = 1ULL << shift;
  size_t offset;
  do {
    offset = prng_() % mod;
  } while (offset >= size);
  return &inputs_[offset];
}

}  // namespace fuzzing
