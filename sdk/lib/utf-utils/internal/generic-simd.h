// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UTF_UTILS_INTERNAL_GENERIC_SIMD_H_
#define LIB_UTF_UTILS_INTERNAL_GENERIC_SIMD_H_

#include <lib/utf-utils/internal/scalar.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <cstring>

// This is an implementation of a SIMD-based validation method based on the lookup method described
// in the paper DOI:10.1002/spe.2920.
//
// To add an implementation for a new set of SIMD instructions, simply implement the following
// skeleton class:
//
// ```cpp
// class ArchImpl {
//  public:
//   class Vector {
//    public:
//     // Alias for the underlying vector type.
//     using Underlying = int;
//
//     // Creates a vector from data loaded from a pointer to an array.
//     //
//     // The array must be at least of size `sizeof(Vector)`.
//     static Vector LoadFromArray(const void* ptr);
//
//     // Creates a vector filled with a value.
//     static Vector Fill(uint8_t val);
//
//     // Creates and fills a vector with up to 32 elements.
//     //
//     // If the vector holds less than 32 elements, the vector ignores the elements in the front of
//     // the array.
//     static Vector Set32(const std::array<uint8_t, 32>& vals);
//
//     // Creates and fills a vector with 16 elements.
//     //
//     // If the vector holds more than 16 elements, the elements are repeated in chunks of 16.
//     static Vector SetRepeat16(const std::array<uint8_t, 16>& vals);
//
//     // Returns whether the vector is all zero.
//     bool IsAllZero() const;
//
//     // Returns whether all bytes in the vector are ASCII.
//     bool IsAscii() const;
//
//     // Returns the underlying vector.
//     Underlying& value();
//
//     // Returns the underlying vector.
//     const Underlying& value() const;
//
//     // Performs a logical OR on every byte between two vectors.
//     friend Vector operator|(const Vector& a, const Vector& b);
//     Vector& operator|=(const Vector& other);
//
//     // Performs a logical AND on every byte between two vectors.
//     friend Vector operator&(const Vector& a, const Vector& b);
//
//     // Performs a logical XOR on every byte between two vectors.
//     friend Vector operator^(const Vector& a, const Vector& b);
//
//     // Performs a saturating subtraction on every byte in the vector, given a subtrahend.
//     Vector SaturatingSub(const Vector& subtrahend) const;
//
//     // Shifts the vector contents right by 4.
//     Vector Shr4() const;
//
//     // Grabs `N` bytes from the lower part of the previous vector, `prev`, and in a new vector,
//     // replaces the upper parts of the current vector with the `N` bytes.
//     template <size_t N>
//     Vector Prev(const Vector& prev) const;
//
//     // Performs a vector table lookup.
//     Vector Lookup16(const std::array<uint8_t, 16>& table) const;
//
//     // Stores the vector to an array.
//     //
//     // The array must be at least of size `sizeof(Vector)`.
//     void StoreToArray(void *ptr);
//   };
//
//   // Prefetch data at a given address.
//   static void Prefetch(const void* ptr);
//
//   // Returns the size of the vector.
//   static constexpr size_t VectorSize();
//
//   // Checks for continuation bytes in previous vectors.
//   static Vector Check2Or3Continuation(const Vector& prev2, const Vector& prev3);
// };
// ```
//
// The resulting class can then be passed as a template parameter to `IsValidUtf8Simd()`, which runs
// the generic algorithm.

namespace utfutils {
namespace internal {

// Helper class to help group vectors into chunks. This helps unroll loops into (slightly) larger
// strings of vectorized instructions to reduce the frequency (and thus, cost) of branching checks.
template <typename ArchImpl>
class VectorChunk {
 private:
  using Vector = typename ArchImpl::Vector;

  // Target a common cache line size as the size of chunks.
  static constexpr size_t kTargetChunkSize = 64;

  static constexpr size_t GetVectorCount() {
    return std::max(kTargetChunkSize / ArchImpl::VectorSize(), size_t{1});
  }

 public:
  static constexpr size_t GetSize() { return ArchImpl::VectorSize() * GetVectorCount(); }

  static VectorChunk LoadFromArray(const void* ptr) {
    VectorChunk chunk;

    for (size_t i = 0; i < chunk.GetVectorCount(); ++i) {
      chunk.vectors()[i] =
          Vector::LoadFromArray(static_cast<const uint8_t*>(ptr) + (i * ArchImpl::VectorSize()));
    }

    return chunk;
  }

  const std::array<Vector, GetVectorCount()>& vectors() const { return chunk_; }
  std::array<Vector, GetVectorCount()>& vectors() { return chunk_; }

  bool IsAscii() const {
    Vector result;

    // Specialize cases to reduce data dependencies
    if (GetVectorCount() == 4) {
      Vector v1 = chunk_[0] | chunk_[1];
      Vector v2 = chunk_[2] | chunk_[3];

      result = v1 | v2;
    } else if (GetVectorCount() == 2) {
      result = chunk_[0] | chunk_[1];
    } else {
      result = Vector::Fill(0);
      for (const auto& vec : chunk_) {
        result |= vec;
      }
    }

    return result.IsAscii();
  }

  void StoreToArray(void* ptr) {
    for (size_t i = 0; i < GetVectorCount(); ++i) {
      vectors()[i].StoreToArray(static_cast<uint8_t*>(ptr) + (i * ArchImpl::VectorSize()));
    }
  }

 private:
  std::array<Vector, GetVectorCount()> chunk_;
};

// Classify each byte according to the rules it must adhere to.
template <typename Vector>
Vector ClassifyRules(const Vector& cur, const Vector& prev) {
  // The rules are as follows:
  //  * Too Short: the leading byte must be followed by N-1 continuation bytes, where N is the UTF-8
  //    codepoint length.
  //  * Too Long: the leading byte must not be a continuation byte.
  //  * Overlong: the character must be above U+7F for 2-byte codepoints, U+7FF for 3-byte
  //    codepoints, and U+FFFF for four-byte characters.
  //  * Too Large: the character must be <= U+10FFFF.
  //  * Surrogate: the character must not be in the range [U+D800, U+DFFF].

  constexpr uint8_t kTooShort = 1 << 0;      // Bad: lead byte -> lead byte, lead byte -> ASCII
  constexpr uint8_t kTooLong = 1 << 1;       // Bad: ASCII -> continuation byte
  constexpr uint8_t kOverlong2 = 1 << 2;     // Bad: 11100000 100*****
  constexpr uint8_t kTooLarge1001 = 1 << 3;  // Bad: too large with second byte 101**** or 1001****
  constexpr uint8_t kSurrogate = 1 << 4;     // Bad: surrogate code point
  constexpr uint8_t kOverlong3 = 1 << 5;     // Bad: 1100000* 10******
  constexpr uint8_t kTooLarge1000 = 1 << 6;  // Bad: too large with second byte 1000****
  constexpr uint8_t kOverlong4 = 1 << 6;     // Bad: 11110000 1000****
  constexpr uint8_t kTwoContinuations = 1 << 7;  // This, by itself, is not invalid.

  // For codepoints that allow anything in the first 4 bits of the first byte.
  constexpr uint8_t kCarry = kTooShort | kTooLong | kTwoContinuations;

  Vector prev1 = cur.template Prev<1>(prev);

  Vector byte1_lo =
      (prev1 & Vector::Fill(0xF))
          .Lookup16({kCarry | kOverlong2 | kOverlong3 | kOverlong4, kCarry | kOverlong3, kCarry,
                     kCarry, kCarry | kTooLarge1001, kCarry | kTooLarge1001 | kTooLarge1000,
                     kCarry | kTooLarge1001 | kTooLarge1000, kCarry | kTooLarge1001 | kTooLarge1000,
                     kCarry | kTooLarge1001 | kTooLarge1000, kCarry | kTooLarge1001 | kTooLarge1000,
                     kCarry | kTooLarge1001 | kTooLarge1000, kCarry | kTooLarge1001 | kTooLarge1000,
                     kCarry | kTooLarge1001 | kTooLarge1000,
                     kCarry | kTooLarge1001 | kTooLarge1000 | kSurrogate,
                     kCarry | kTooLarge1001 | kTooLarge1000,
                     kCarry | kTooLarge1001 | kTooLarge1000});

  Vector byte1_hi = prev1.Shr4().Lookup16(
      {kTooLong, kTooLong, kTooLong, kTooLong, kTooLong, kTooLong, kTooLong, kTooLong,
       kTwoContinuations, kTwoContinuations, kTwoContinuations, kTwoContinuations,
       kTooShort | kOverlong3, kTooShort, kTooShort | kOverlong2 | kSurrogate,
       kTooShort | kTooLarge1001 | kTooLarge1000 | kOverlong4});

  Vector byte2_hi = cur.Shr4().Lookup16(
      {kTooShort, kTooShort, kTooShort, kTooShort, kTooShort, kTooShort, kTooShort, kTooShort,
       kTooLong | kOverlong3 | kTwoContinuations | kOverlong2 | kTooLarge1000 | kOverlong4,
       kTooLong | kOverlong3 | kTwoContinuations | kOverlong2 | kTooLarge1001,
       kTooLong | kOverlong3 | kTwoContinuations | kSurrogate | kTooLarge1001,
       kTooLong | kOverlong3 | kTwoContinuations | kSurrogate | kTooLarge1001, kTooShort, kTooShort,
       kTooShort, kTooShort});

  return byte1_lo & byte1_hi & byte2_hi;
}

// Checks that the multi-byte codepoints are of appropriate length.
template <typename ArchImpl>
typename ArchImpl::Vector CheckMultiByte(const typename ArchImpl::Vector& cur,
                                         const typename ArchImpl::Vector& prev) {
  auto prev2 = cur.template Prev<2>(prev);
  auto prev3 = cur.template Prev<3>(prev);

  return ArchImpl::Check2Or3Continuation(prev2, prev3) & ArchImpl::Vector::Fill(0x80);
}

// Checks whether a vector, if terminal, would be incomplete UTF-8.
template <typename Vector>
Vector CheckIncomplete(const Vector& vec) {
  return vec.SaturatingSub(Vector::Set32(
      {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,       0xFF,       0xFF,      0xFF,
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,       0xFF,       0xFF,      0xFF,
       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0b11101111, 0b11011111, 0b10111111}));
}

template <typename ArchImpl>
void ProcessChunk(const VectorChunk<ArchImpl>& cur, typename ArchImpl::Vector& prev,
                  typename ArchImpl::Vector& error) {
  using Vector = typename ArchImpl::Vector;

  if (cur.IsAscii()) {
    // If this chunk is all ASCII, then the last chunk must not have continuation bytes that
    // overflow the previous chunk and into this chunk.
    error |= CheckIncomplete(prev);
    prev = cur.vectors().back();
  } else {
    // Chunk is not all ASCII so check rules and multibyte.
    for (auto& vec : cur.vectors()) {
      Vector rule_check = ClassifyRules(vec, prev);
      Vector multibyte = CheckMultiByte<ArchImpl>(vec, prev);

      error |= (rule_check ^ multibyte);
      prev = vec;
    }
  }
}

// Runs the generic SIMD validate and store algorithm on a particular architecture implementation.
//
// Note the `dst` parameter must only be valid if template parameter `do_copy` is set.
template <typename ArchImpl, bool do_copy>
bool RunValidateAndCopyUtf8Simd(const char* src, __attribute__((unused)) char* dst,
                                const size_t size) {
  using Vector = typename ArchImpl::Vector;

  // Use the scalar path for small strings.
  constexpr size_t kSmallStringSize = VectorChunk<ArchImpl>::GetSize() * 3 / 2;
  if (size < kSmallStringSize) {
    if /* constexpr */ (do_copy) {
      return ValidateAndCopyUtf8Scalar(src, dst, size);
    } else {
      return IsValidUtf8Scalar(src, size);
    }
  }

  if (src == nullptr) {
    return false;
  }

  auto prev = Vector::Fill(0);
  auto error = Vector::Fill(0);
  const size_t aligned_size = size - (size % VectorChunk<ArchImpl>::GetSize());

  size_t offset = 0;
  bool encountered_non_ascii = false;

  // Fast path for ASCII
  for (; offset < aligned_size; offset += VectorChunk<ArchImpl>::GetSize()) {
    auto cur = VectorChunk<ArchImpl>::LoadFromArray(src + offset);
    if (__builtin_expect(!cur.IsAscii(), 0)) {
      encountered_non_ascii = true;
      break;
    }

    if /* constexpr */ (do_copy) {
      cur.StoreToArray(dst + offset);
    }

    // Note that it's not necessary to assign `prev = cur` here since `prev` is initialized to all
    // zeros, which is valid ASCII. If a non-ASCII sequence were to be detected in this loop and
    // break early, the previous chunk (before the one that triggered the break) was some valid
    // sequence of ASCII characters. The exact contents of that sequence does not matter.
  }

  // Process chunks of vectors.
  for (; offset < aligned_size; offset += VectorChunk<ArchImpl>::GetSize()) {
    // Prefetch the next chunk of data.
    ArchImpl::Prefetch(src + offset + VectorChunk<ArchImpl>::GetSize());

    auto cur = VectorChunk<ArchImpl>::LoadFromArray(src + offset);
    if /* constexpr */ (do_copy) {
      cur.StoreToArray(dst + offset);
    }
    ProcessChunk<ArchImpl>(cur, prev, error);
  }

  if (offset < size) {
    if (!encountered_non_ascii) {
      // If a non-ASCII character has yet to be encountered, use the scalar implementation, which is
      // quicker for very small strings.
      if /* constexpr */ (do_copy) {
        return ValidateAndCopyUtf8Scalar(src + offset, dst + offset, size - offset);
      } else {
        return IsValidUtf8Scalar(src + offset, size - offset);
      }
    }

    // Copy remaining data into a zero-initialized chunk.
    VectorChunk<ArchImpl> cur = {};
    memcpy(&cur, src + offset, size - offset);
    if /* constexpr */ (do_copy) {
      memcpy(dst + offset, src + offset, size - offset);
    }

    ProcessChunk<ArchImpl>(cur, prev, error);
  }

  if (__builtin_expect(!prev.IsAscii(), 0)) {
    // The previous chunk was not all ASCII, so check that there wasn't an incomplete multibyte
    // codepoint hanging at the end.
    error |= CheckIncomplete(prev);
  }

  return error.IsAllZero();
}

template <typename ArchImpl>
bool IsValidUtf8Simd(const char* data, size_t size) {
  return RunValidateAndCopyUtf8Simd<ArchImpl, false>(data, nullptr, size);
}

template <typename ArchImpl>
bool ValidateAndCopyUtf8Simd(const char* src, char* dst, size_t size) {
  return RunValidateAndCopyUtf8Simd<ArchImpl, true>(src, dst, size);
}

}  // namespace internal
}  // namespace utfutils

#endif  // LIB_UTF_UTILS_INTERNAL_GENERIC_SIMD_H_
