// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_GNU_HASH_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_GNU_HASH_H_

#include <lib/stdcompat/bit.h>
#include <lib/stdcompat/span.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "layout.h"

namespace elfldltl {

// This handles the DT_GNU_HASH format, which is the de facto standard format.
// This interface matches CompatHash (compat-hash.h).
// See SymbolInfo (symbol.h) for details.

inline constexpr uint32_t GnuHashString(std::string_view name) {
  uint_fast32_t hash = 5381;
  for (char c : name) {
    hash *= 33;
    hash += cpp20::bit_cast<unsigned char>(c);
  }
  return static_cast<uint32_t>(hash);
}

constexpr uint32_t kGnuNoHash = uint32_t{};

// The DT_GNU_HASH format provides a Bloom filter and a hash table.  The data
// is always aligned to address size but starts with a header of four uint32_t
// words regardless of address size:
//
//  * nbucket: number of hash buckets
//  * bias: chain table index bias
//  * nfilter: power-of-two number of Bloom filter array elements
//  * shift: Bloom filter shift count
//
// After the header is an array of address-size words that forms the Bloom
// filter.  The string hash value divided by address-size in bits (i.e. 32 or
// 64), modulo the size of the array (which is required to be a power of two)
// is used as the index into this array, yielding an address-sized bitmask.
// Two bit indices are derived from the string hash value: the hash value
// modulo address-size in bits; and the hash value right-shifted by the shift
// count, modulo address-size in bits.  The bits at both indices are set in
// the bitmask to indicate that this hash value may be present in the table;
// if either bit is clear, no string with this hash value is present.
//
// Then comes the array of uint32_t hash buckets, indexed by the string hash
// value modulo the number of buckets.  Zero indicates an empty hash bucket,
// and other values are symbol table indices.  This points to the first symbol
// in that hash bucket.  Additional symbols in the same bucket are consecutive
// in the symbol table.
//
// The remainder of the data forms an uint32_t array called the "chain table",
// indexed by the index into the symbol table minus the chain table index bias.
// The chain table element corresponding to a symbol table element holds the
// high 31 bits of that symbol's name string's hash value.  The low bit is zero
// if the subsequent element resides in the same hash bucket and one if not.
//
// So the lookup procedure is as follows:
//
//  * Compute the string hash value.
//
//  * Compute the index to the Bloom filter element.
//
//  * Check the filter element.  If it says the hash value is definitely
//    not present in the table, the lookup has failed.
//
//  * Find the first candidate symbol table index in the hash bucket.
//    If this is zero, the lookup has failed.
//
//  * Find the corresponding chain table element (symtab index - bias).
//
//  * Compare the high 31 bits of the hash value to table element's high bits.
//    If those match, then compare the name strings.  (The element's index
//    into the chain table + bias gives the symtab index.)  If the name
//    strings match, the lookup has succeeded, yielding the symtab index.
//
//  * If the low bit of the table element is zero, advance to the next
//    table element and repeat the last step.  If this reaches the end
//    of the table, the table's format is invalid (there must be an entry
//    with the low bit set to terminate each hash bucket's run).
//
//  * Otherwise (i.e. the low bit is one), the lookup has failed.
//

template <typename Word, typename Addr>
class GnuHash {
 public:
  using size_type = typename Addr::value_type;

  class BucketIterator;

  constexpr explicit GnuHash(cpp20::span<const Addr> table) : GnuHash(table, *GetSizes(table)) {}

  static constexpr bool Valid(cpp20::span<const Addr> table) { return GetSizes(table).has_value(); }

  constexpr uint32_t size() const {
    // First run through the buckets to find the largest symbol table index.
    uint32_t max_symndx = 0;

    if constexpr (sizeof(Addr) == sizeof(uint32_t)) {
      auto buckets = tables_.subspan(filter_index_mask_ + 1, bucket_count_);
      for (uint32_t symndx : buckets) {
        max_symndx = std::max(symndx, max_symndx);
      }
    } else {
      size_t word_count = bucket_count_ / kBucketsPerAddr;
      auto words = tables_.subspan(filter_index_mask_ + 1, word_count);
      for (uint64_t word : words) {
        uint32_t first = static_cast<uint32_t>(word >> Shift(0));
        uint32_t second = static_cast<uint32_t>(word >> Shift(1));
        max_symndx = std::max(std::max(first, second), max_symndx);
      }
      if (bucket_count_ & 1) {
        // The last bucket shares a word with the start of the chain table.
        uint64_t word = tables_[filter_index_mask_ + 1 + word_count];
        uint32_t symndx = static_cast<uint32_t>(word >> Shift(0));
        max_symndx = std::max(symndx, max_symndx);
      }
    }

    if (max_symndx == 0) {
      return 0;
    }

    // Now start at that place in the chain table and count how many remain.

    if constexpr (sizeof(Addr) == sizeof(uint32_t)) {
      auto chain = tables_.subspan(filter_index_mask_ + 1 + bucket_count_);
      if (max_symndx > chain_index_bias_ ||  // Check for bogus index.
          max_symndx - chain_index_bias_ >= chain.size()) [[unlikely]] {
        return 0;
      }
      for (uint32_t hash : chain.subspan(max_symndx - chain_index_bias_)) {
        ++max_symndx;
        if (hash & 1) {
          return max_symndx;
        }
      }
    } else {
      if (max_symndx > chain_index_bias_) [[unlikely]] {
        return 0;
      }

      auto words = tables_.subspan(filter_index_mask_ + 1);

      uint32_t offset = BucketChainStart(max_symndx);

      if ((offset >> 1) > words.size()) [[unlikely]] {
        return 0;
      }

      if (offset & 1) {
        // The first element of interest shares a word with the previous one.
        ++max_symndx;
        if (words[offset >> 1] & kSecondEnd) {
          return max_symndx;
        }
        ++offset;
      }

      // Check the remaining words two at a time for the end marker.
      offset >>= 1;
      if (offset >= words.size()) [[unlikely]] {
        return 0;
      }

      for (uint64_t word : words.subspan(offset)) {
        ++max_symndx;
        if (word & kFirstEnd) {
          return max_symndx;
        }
        ++max_symndx;
        if (word & kSecondEnd) {
          return max_symndx;
        }
      }
    }

    return 0;  // Table didn't end with an end marker.
  }

  constexpr uint32_t Bucket(uint32_t hash) const {
    size_type filter = tables_[(hash / kAddrBits) & filter_index_mask_];
    uint32_t bit1 = hash % kAddrBits;
    uint32_t bit2 = (hash >> filter_hash_shift_) % kAddrBits;
    if ((filter >> bit1) & (filter >> bit2) & 1) {
      uint32_t bucket = hash % bucket_count_;
      if constexpr (sizeof(Addr) == sizeof(uint32_t)) {
        return tables_[filter_index_mask_ + 1 + bucket];
      } else {
        uint64_t word = tables_[filter_index_mask_ + 1 + (bucket >> 1)];
        return static_cast<uint32_t>(word >> Shift(bucket));
      }
    }
    return 0;
  }

 private:
  // The DT_GNU_HASH data is always a whole number of Addr-aligned units, and
  // so comes in as span<const Addr>.  The actual encoding uses Addr-sized
  // elements for the Bloom filter array but Word-sized (i.e. always 32-bit)
  // elements for the symtab index tables and the header fields.
  //
  // The data begins with this four-word header (all 32-bit fields):
  //  * nbucket
  //  * bias
  //  * nfilter
  //  * shift
  // Next follows the Bloom filter array: Addr filter[nfilter].
  // Then comes the array of hash buckets: Word[nbucket].
  // Finally the rest of the words are the "chain" array: Word[].

  struct Sizes {
    uint32_t nbucket;  // Number of buckets.
    uint32_t bias;     // Lowest symtab index representable in the table.
    uint32_t nfilter;  // Number of filter words.
    uint32_t shift;    // Bit-shift on hash values for the Bloom filter.
  };
  static constexpr uint32_t kAddrPerSizes = sizeof(Sizes) / sizeof(Addr);
  static constexpr uint32_t kWordPerSizes = sizeof(Sizes) / sizeof(Word);
  static constexpr uint32_t kBucketsPerAddr = sizeof(Addr) / sizeof(Word);

  static constexpr uint32_t kAddrBits = sizeof(Addr) * 8;

  static constexpr bool kLittle = std::is_same_v<Word, typename Elf32<ElfData::k2Lsb>::Word>;

  // End marker bit in the first or second uint32_t within the word.
  static constexpr uint64_t kFirstEnd = uint64_t{1} << (kLittle ? 0 : 32);
  static constexpr uint64_t kSecondEnd = uint64_t{1} << (kLittle ? 32 : 0);

  // Right-shift applied to a table Addr for the Word at the given index.
  static constexpr uint32_t Shift(uint32_t idx) {
    if constexpr (sizeof(Addr) == sizeof(uint32_t)) {
      return 0;
    }
    return 32 * (kLittle ? (idx & 1) : (~idx & 1));
  }

  constexpr GnuHash(cpp20::span<const Addr> table, const Sizes& sizes)
      : tables_(table.subspan(kAddrPerSizes)),
        bucket_count_(sizes.nbucket),
        chain_index_bias_(sizes.bias),
        filter_index_mask_(sizes.nfilter - 1),
        filter_hash_shift_(sizes.shift) {}

  static constexpr std::optional<Sizes> GetSizes(cpp20::span<const Addr> table) {
    if (table.size() >= kAddrPerSizes) [[likely]] {
      Sizes sizes;
      if constexpr (sizeof(Addr) == sizeof(Word)) {
        sizes = {
            .nbucket = table[0],
            .bias = table[1],
            .nfilter = table[2],
            .shift = table[3],
        };
      } else {
        uint64_t first = table[0];
        uint64_t second = table[1];
        sizes = {
            .nbucket = static_cast<uint32_t>(first >> Shift(0)),
            .bias = static_cast<uint32_t>(first >> Shift(1)),
            .nfilter = static_cast<uint32_t>(second >> Shift(0)),
            .shift = static_cast<uint32_t>(second >> Shift(1)),
        };
      }

      const size_t total_addrs = table.size() - kAddrPerSizes;

      // There must be one slot for each bucket, followed by at least one more
      // slot for the chain table.  This minimum number of slots can be rounded
      // up to the number of slots per Addr, since there is always a whole
      // number of Addr words in the overall table.
      const uint32_t bucket_slots = (sizes.nbucket + 1 + kBucketsPerAddr - 1) / kBucketsPerAddr;
      const uint32_t max_buckets = bucket_slots * kBucketsPerAddr;

      if (sizes.nbucket > 0 &&                     // Cannot be empty.
          sizes.nbucket <= max_buckets &&          // Check for overflow.
          sizes.shift < 32 &&                      // Must be plausible.
          cpp20::has_single_bit(sizes.nfilter) &&  // Must be power of two.
          total_addrs >= sizes.nfilter &&          // Space for the filters.
          // There must be space for the buckets and the chain table.  We can't
          // really tell how much space is needed for the chain table without
          // examining all the buckets, so those indices can't be presumed
          // valid later.
          total_addrs - sizes.nfilter >= bucket_slots) [[likely]] {
        return sizes;
      }
    }
    return std::nullopt;
  }

  // Return Word index for the start of the bucket's chain table, relative to
  // the start of the bucket table.
  constexpr uint32_t BucketChainStart(uint32_t symndx) const {
    return symndx - chain_index_bias_ + bucket_count_;
  }

  // Return Word index into tables_ for the start of the bucket's chain table.
  constexpr uint32_t AbsoluteBucketChainStart(uint32_t symndx) const {
    return ((filter_index_mask_ + 1) * kBucketsPerAddr) + BucketChainStart(symndx);
  }

  cpp20::span<const Addr> tables_;
  uint32_t bucket_count_ = 0;
  uint32_t chain_index_bias_ = 0;
  uint32_t filter_index_mask_ = 0;
  uint32_t filter_hash_shift_ = 0;
};

template <typename Word, typename Addr>
class GnuHash<Word, Addr>::BucketIterator {
 public:
  constexpr BucketIterator() = default;
  constexpr BucketIterator(const BucketIterator&) = default;
  constexpr BucketIterator& operator=(const BucketIterator&) = default;

  // This creates a begin() iterator for the bucket and hash value.
  constexpr explicit BucketIterator(const GnuHash& table, uint32_t bucket, uint32_t hash)
      : table_(table),
        i_(table.AbsoluteBucketChainStart(bucket)),
        // Store with the low bit set for quick comparisons to the chain table.
        hash_(hash | 1) {
    // Check for a bogus index coming from the bucket table.
    const uint32_t idx = i_ / kBucketsPerAddr;  // Word index -> tables_ index.
    if (bucket < table_.chain_index_bias_ || idx >= table_.tables_.size()) [[unlikely]] {
      GoToEnd();
    } else {
      // We're now pointing to the start of the bucket.
      // Advance to the first symbol matching the hash value.
      AdvanceToNextHashMatch(table_.tables_[idx]);
    }
  }

  // This creates an end() iterator.
  constexpr explicit BucketIterator(const GnuHash& table)
      : table_(table), i_(static_cast<uint32_t>(table.tables_.size() * kBucketsPerAddr)) {}

  constexpr bool operator==(const BucketIterator& other) const { return i_ == other.i_; }

  constexpr bool operator!=(const BucketIterator& other) const { return !(*this == other); }

  constexpr BucketIterator& operator++() {  // prefix
    // The current chain word was the previous match for hash_.
    size_type current = table_.tables_[i_];

    // Check the current entry for the end marker.
    if ((current >> Shift(i_)) & 1) {
      GoToEnd();
    } else {
      // Look at the rest of the bucket.
      ++i_;
      AdvanceToNextHashMatch(current);
    }

    return *this;
  }

  constexpr BucketIterator& operator++(int) {  // postfix
    auto old = *this;
    ++*this;
    return old;
  }

  constexpr uint32_t operator*() const { return i_ - table_.AbsoluteBucketChainStart(0); }

 private:
  constexpr void GoToEnd() { i_ = static_cast<uint32_t>(table_.tables_.size() * kBucketsPerAddr); }

  constexpr void AdvanceToNextHashMatch(size_type current) {
    if constexpr (sizeof(Addr) == sizeof(uint32_t)) {
      for (uint32_t chain : table_.tables_.subspan(i_)) {
        if ((chain | 1) == hash_) {
          // Found a matching entry.
          return;
        }

        if (chain & 1) {
          // Hit the end marker with no matches.
          GoToEnd();
          return;
        }

        // Advance to the next entry.
        ++i_;
      }
    } else {
      if (i_ & 1) {
        // Check the second half of the current word.
        uint32_t chain = static_cast<uint32_t>(current >> Shift(1));
        if ((chain | 1) == hash_) {
          // Found a matching entry.
          return;
        }

        if (chain & 1) {
          // Hit the end marker with no matches.
          GoToEnd();
          return;
        }

        // Advance to the next (aligned) entry.
        ++i_;
      }

      // Now check two words at a time.
      for (uint64_t word : table_.tables_.subspan(i_ >> 1)) {
        uint32_t chain = static_cast<uint32_t>(word >> Shift(0));
        if ((chain | 1) == hash_) {
          // Found a matching entry.
          return;
        }

        if (chain & 1) {
          // Hit the end marker with no matches.
          GoToEnd();
          return;
        }

        // Advance to the second entry of the pair.
        ++i_;
        chain = static_cast<uint32_t>(word >> Shift(1));

        if ((chain | 1) == hash_) {
          // Found a matching entry.
          return;
        }

        if (chain & 1) {
          // Hit the end marker with no matches.
          GoToEnd();
          return;
        }

        // Advance to the next pair of entries.
        ++i_;
      }
    }
  }

  const GnuHash& table_;
  uint32_t i_ = -1;
  uint32_t hash_ = 0;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_GNU_HASH_H_
