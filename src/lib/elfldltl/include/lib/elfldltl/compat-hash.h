// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_COMPAT_HASH_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_COMPAT_HASH_H_

#include <lib/stdcompat/bit.h>
#include <lib/stdcompat/span.h>

#include <cstdint>
#include <string_view>

namespace elfldltl {

// This handles the DT_HASH format, which is mostly obsolete but is the
// official ELF standard format.  This interface matches GnuHash (gnu-hash.h).
// See SymbolInfo (symbol.h) for details.

inline constexpr uint32_t CompatHashString(std::string_view name) {
  uint_fast32_t hash = 0;
  for (char c : name) {
    hash = (hash << 4) + cpp20::bit_cast<unsigned char>(c);
    hash ^= (hash >> 24) & 0xf0;
  }
  return hash & 0x0fffffff;
}

constexpr uint32_t kCompatNoHash = ~uint32_t{};

// In DT_HASH format, there is a table mapping hash buckets to indices of the
// first symbol table entry in the bucket.  A second "chain" table maps the
// symbol table index of each symbol to the next symbol in the same bucket.
// Empty buckets and the end of a chain are identified by index 0 (STN_UNDEF),
// which is always a null entry.  The first two words of the DT_HASH data are
// the number of buckets and the number of chain entries (i.e. the number of
// symbol table entries).  Then the bucket words follow, then the chain words.

template <typename Word>
class CompatHash {
 public:
  class BucketIterator;

  constexpr explicit CompatHash(cpp20::span<const Word> table)
      : buckets_(table.subspan(2, table[0])), chain_(table.subspan(2 + table[0], table[1])) {}

  static constexpr bool Valid(cpp20::span<const Word> table) {
    if (table.size() < 2) {
      return false;
    }
    const uint32_t nbucket = table[0], nchain = table[1];
    return table.size() - 2 > nbucket && table.size() - 2 - nbucket >= nchain;
  }

  constexpr uint32_t size() const { return static_cast<uint32_t>(chain_.size()); }

  constexpr uint32_t Bucket(uint32_t hash) const {
    if (buckets_.empty()) [[unlikely]] {
      return 0;
    }
    return buckets_[hash % buckets_.size()];
  }

 private:
  cpp20::span<const Word> buckets_, chain_;
};

template <typename Word>
class CompatHash<Word>::BucketIterator {
 public:
  constexpr BucketIterator() = default;
  constexpr BucketIterator(const BucketIterator&) = default;
  constexpr BucketIterator& operator=(const BucketIterator&) = default;

  // This creates a begin() iterator for the bucket and hash value.
  constexpr explicit BucketIterator(const CompatHash& table, uint32_t bucket, uint32_t hash)
      : chain_(table.chain_), i_(BucketIndex(bucket)) {}

  // This creates an end() iterator.
  constexpr explicit BucketIterator(const CompatHash& table) : chain_(table.chain_) {}

  constexpr bool operator==(const BucketIterator& other) const { return i_ == other.i_; }

  constexpr bool operator!=(const BucketIterator& other) const { return !(*this == other); }

  constexpr BucketIterator& operator++() {  // prefix
    // The chain table might encode an infinite loop here.  So cut short
    // iteration when the total number of entries has been enumerated.  In
    // corrupt data, this may not have covered all the entries because it hit a
    // loop.  In valid data, the natural end will always be reached first.
    if (++count_ > chain_.size()) [[unlikely]] {
      i_ = 0;
    } else {
      i_ = BucketIndex(chain_[i_]);
    }
    return *this;
  }

  constexpr BucketIterator& operator++(int) {  // postfix
    auto old = *this;
    ++*this;
    return old;
  }

  constexpr uint32_t operator*() const { return i_; }

 private:
  // If a bogus index came out of the table, reset to end() state.
  constexpr uint32_t BucketIndex(uint32_t symndx) {
    if (symndx < chain_.size()) [[likely]] {
      return symndx;
    }
    return 0;
  }

  cpp20::span<const Word> chain_;
  uint32_t i_ = 0;
  uint32_t count_ = 0;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_COMPAT_HASH_H_
