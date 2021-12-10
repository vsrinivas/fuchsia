// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/corpus.h"

#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls.h>

#include <algorithm>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace fuzzing {

// Public methods

Corpus::Corpus() { inputs_.emplace_back(); }

Corpus& Corpus::operator=(Corpus&& other) noexcept {
  options_ = other.options_;
  other.options_ = nullptr;
  prng_ = other.prng_;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::lock_guard<std::mutex> other_lock(other.mutex_);
    inputs_ = std::move(other.inputs_);
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

zx_status_t Corpus::LoadAt(const std::string& root, const std::vector<std::string>& dirs) {
  for (const auto& dirname : dirs) {
    auto status = ReadDir(files::JoinPath(root, dirname));
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Corpus::Load(const std::vector<std::string>& dirs) { return LoadAt("/pkg", dirs); }

zx_status_t Corpus::ReadDir(const std::string& dirname) {
  std::vector<std::string> contents;
  if (!files::ReadDirContents(dirname, &contents)) {
    FX_LOGS(ERROR) << "No such corpus directory: " << dirname << " (errno=" << errno << ")";
    return ZX_ERR_NOT_FOUND;
  }
  for (const auto& dir_entry : contents) {
    if (dir_entry == ".") {
      continue;
    }
    auto pathname = files::SimplifyPath(files::JoinPath(dirname, dir_entry));
    zx_status_t status = ZX_OK;
    if (files::IsFile(pathname)) {
      status = ReadFile(pathname);
    } else if (files::IsDirectory(pathname)) {
      status = ReadDir(pathname);
    }
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t Corpus::ReadFile(const std::string& filename) {
  std::vector<uint8_t> input;
  if (!files::ReadFileToVector(filename, &input)) {
    FX_LOGS(ERROR) << "Failed to read " << filename;
    return ZX_ERR_IO;
  }
  return Add(Input(input));
}

zx_status_t Corpus::Add(Input input) {
  std::lock_guard<std::mutex> lock(mutex_);
  FX_DCHECK(options_);
  if (input.size() > options_->max_input_size()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  // Keep the inputs sorted and deduplicated.
  auto iter = std::lower_bound(inputs_.begin(), inputs_.end(), input);
  if (iter == inputs_.end() || *iter != input) {
    total_size_ += input.size();
    inputs_.insert(iter, std::move(input));
  }
  return ZX_OK;
}

bool Corpus::At(size_t offset, Input* out) {
  out->Clear();
  std::lock_guard<std::mutex> lock(mutex_);
  if (offset >= inputs_.size()) {
    return false;
  }
  out->Duplicate(inputs_[offset]);
  return true;
}

void Corpus::Pick(Input* out) {
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
  out->Duplicate(inputs_[offset]);
}

}  // namespace fuzzing
