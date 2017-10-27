/*
 * Copyright 2011 Avery Pennarun. All rights reserved.
 *
 * (This license applies to bupsplit.c and bupsplit.h only.)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AVERY PENNARUN ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "peridot/third_party/bup/bupsplit.h"

#include <memory.h>
#include <stdint.h>

namespace bup {

namespace {

// According to librsync/rollsum.h:
// "We should make this something other than zero to improve the
// checksum algorithm: tridge suggests a prime number."
// apenwarr: I unscientifically tried 0 and 7919, and they both ended up
// slightly worse than the librsync value of 31 for my arbitrary test data.
constexpr uint8_t kRollsumCharOffset = 31;

}  // namespace

RollSumSplit::RollSumSplit(size_t min_length, size_t max_length)
    : min_length_(min_length), max_length_(max_length) {
  Reset();
}

RollSumSplit::RollSumSplit(const RollSumSplit& other) = default;

void RollSumSplit::Reset() {
  current_length_ = 0u;
  s1_ = kWindowSize * kRollsumCharOffset;
  s2_ = kWindowSize * (kWindowSize - 1) * kRollsumCharOffset;
  window_index_ = 0;
  memset(window_, 0, kWindowSize);
}

size_t RollSumSplit::Feed(fxl::StringView buffer, size_t* bits) {
  for (size_t i = 0; i < buffer.size(); i++) {
    Roll(buffer[i]);
    ++current_length_;
    if (current_length_ >= min_length_ &&
        ((s2_ & (kBlobSize - 1)) == ((~0) & (kBlobSize - 1)) ||
         current_length_ >= max_length_)) {
      if (bits) {
        uint32_t rsum = Digest();
        *bits = kBlobBits;
        rsum >>= kBlobBits;
        while ((rsum >>= 1) & 1) {
          (*bits)++;
        }
      }
      current_length_ = 0;
      return i + 1;
    }
  }
  return 0;
}

void RollSumSplit::Add(uint8_t drop, uint8_t add) {
  s1_ += add - drop;
  s2_ += s1_ - (kWindowSize * (drop + kRollsumCharOffset));
}

void RollSumSplit::Roll(uint8_t c) {
  Add(window_[window_index_], c);
  window_[window_index_] = c;
  window_index_ = (window_index_ + 1) % kWindowSize;
}

uint32_t RollSumSplit::Digest() {
  return (s1_ << 16) | (s2_ & 0xffff);
}

}  // namespace bup
