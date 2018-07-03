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
#ifndef PERIDOT_BIN_LEDGER_THIRD_PARTY_BUP_BUPSPLIT_H_
#define PERIDOT_BIN_LEDGER_THIRD_PARTY_BUP_BUPSPLIT_H_

#include <stdint.h>

#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

namespace bup {

constexpr uint8_t kBlobBits = 13;
constexpr uint16_t kBlobSize = 1 << kBlobBits;
constexpr uint8_t kWindowBits = 7;
constexpr uint16_t kWindowSize = 1 << (kWindowBits - 1);

// Splits data into chunks between |min_length| and |max_length| of size that
// are "good" for de-duplication.
//
// It achieves that by calculating a rolling hash of the window of
// |kWindowBits|. The split points are selected when the last |kBlobBits| of the
// current hash are all 1s. This ensures that if data is removed or inserted in
// the middle of data, only the 2 split points following the change are
// modified, and all others stay identical.
class RollSumSplit {
 public:
  // |min_length| is the minimal size of a chunk.
  // |max_length| is the maximal size of a chunk.
  RollSumSplit(size_t min_length, size_t max_length);

  // Copy constructor.
  RollSumSplit(const RollSumSplit& other);

  // Reset the state of the rolling hash.
  void Reset();

  // Returns a non-zero value indicating the size of the prefix that is the next
  // cut, or 0 if all data was consumed without finding the next cut.
  // If |bits| is not null, and a cut has been found, |*bits| will be the number
  // of trailing 1s in the current hash. It will always be greater of equals to
  // |kBlobBits|.
  size_t Feed(fxl::StringView buffer, size_t* bits);

 private:
  void Add(uint8_t drop, uint8_t add);
  void Roll(uint8_t c);
  uint32_t Digest();

  const size_t min_length_;
  const size_t max_length_;
  size_t current_length_;
  uint64_t s1_, s2_;
  uint8_t window_[kWindowSize];
  size_t window_index_;
};

}  // namespace bup

#endif  // PERIDOT_BIN_LEDGER_THIRD_PARTY_BUP_BUPSPLIT_H_
