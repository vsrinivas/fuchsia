// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/module-proxy.h"

#include <lib/syslog/cpp/macros.h>

namespace fuzzing {
namespace {

// Convert counter values to "features" in the same manner as AFL, described here:
// http://lcamtuf.coredump.cx/afl/technical_details.txt.
//
// This implementation is based on the one used by libFuzzer. Do not try to optimize this further;
// as written the compiler is able to effectively optimize it at -O2 and higher.
uint8_t to_feature(uint8_t counter) {
  uint8_t feature = 0;
  if (counter >= 128) {
    feature = 1 << 7;
  } else if (counter >= 32) {
    feature = 1 << 6;
  } else if (counter >= 16) {
    feature = 1 << 5;
  } else if (counter >= 8) {
    feature = 1 << 4;
  } else if (counter >= 4) {
    feature = 1 << 3;
  } else if (counter >= 3) {
    feature = 1 << 2;
  } else if (counter >= 2) {
    feature = 1 << 1;
  } else if (counter >= 1) {
    feature = 1 << 0;
  }
  return feature;
}

// High bit in each byte of a uint64_t. See |MeasureImpl|.
constexpr uint64_t kHiBitsMask = 0x80'80'80'80'80'80'80'80ULL;

}  // namespace

ModuleProxy::ModuleProxy(Identifier id, size_t size) : id_(id), num_u64s_(size / sizeof(uint64_t)) {
  // This method expects 64-bit alignment to simplify iteration.
  FX_CHECK(size % sizeof(uint64_t) == 0);
  features_.reset(new uint64_t[num_u64s_]);
  accumulated_.reset(new uint64_t[num_u64s_]);
  Clear();
}

void ModuleProxy::Add(void* counters, size_t counters_len) {
  std::lock_guard<std::mutex> lock(mutex_);
  FX_CHECK(counters_len == size());
  // This method expects 64-bit alignment to simplify iteration.
  FX_CHECK(reinterpret_cast<uintptr_t>(counters) % sizeof(uint64_t) == 0);
  counters_.push_back(reinterpret_cast<uint64_t*>(counters));
}

void ModuleProxy::Remove(void* counters) {
  std::lock_guard<std::mutex> lock(mutex_);
  counters_.erase(
      std::remove(counters_.begin(), counters_.end(), reinterpret_cast<uint64_t*>(counters)),
      counters_.end());
}

size_t ModuleProxy::Measure() { return MeasureImpl(/* accumulate */ false); }

size_t ModuleProxy::Accumulate() { return MeasureImpl(/* accumulate */ true); }

size_t ModuleProxy::MeasureImpl(bool accumulate) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t num_new_features = 0;
  memset(features_.get(), 0, size());
  // First, sum all counters into the features array.
  for (auto* counters : counters_) {
    for (size_t i = 0; i < num_u64s_; ++i) {
      if (counters[i]) {
        // Sums over 128 map to the same feature and don't need to be distinguish. This means we can
        // get the right features by simply adding 7-bits of each byte in parallel and ORing the
        // high bits. This avoids overflowing from one byte to the next.
        uint64_t hi_bits = (counters[i] | features_[i]) & kHiBitsMask;
        features_[i] = ((features_[i] & ~kHiBitsMask) + (counters[i] & ~kHiBitsMask)) | hi_bits;
      }
    }
  }
  // Next, convert the summed counters to features.
  for (size_t i = 0; i < num_u64s_; ++i) {
    if (features_[i]) {
      uint8_t* feature = reinterpret_cast<uint8_t*>(&features_[i]);
      for (size_t j = 0; j < sizeof(uint64_t); ++j) {
        feature[j] = static_cast<uint8_t>(to_feature(feature[j]));
      }
      num_new_features += __builtin_popcountll(~accumulated_[i] & features_[i]);
      if (accumulate) {
        accumulated_[i] |= features_[i];
      }
    }
  }
  return num_new_features;
}

size_t ModuleProxy::GetCoverage(size_t* out_num_features) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t num_pcs = 0;
  size_t num_features = 0;
  for (size_t i = 0; i < num_u64s_; ++i) {
    auto accumulated = accumulated_[i];
    if (accumulated) {
      num_features += __builtin_popcountll(accumulated_[i]);
      for (; accumulated; accumulated >>= 8) {
        if (accumulated & 0xff) {
          num_pcs++;
        }
      }
    }
  }
  if (out_num_features) {
    *out_num_features = num_features;
  }
  return num_pcs;
}

void ModuleProxy::Clear() { memset(accumulated_.get(), 0, size()); }

}  // namespace fuzzing
