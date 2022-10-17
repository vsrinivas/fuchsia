// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Definitions to provide a  Writer interface for FXT. We want a userspace
// writer and a kernel writer that may have different implementations, but
// don't need to dynamically switch between them, so we enforce the required
// methods through type traits, rather than, say, an abstract class.

#ifndef SRC_PERFORMANCE_LIB_FXT_INCLUDE_LIB_FXT_WRITER_INTERNAL_H_
#define SRC_PERFORMANCE_LIB_FXT_INCLUDE_LIB_FXT_WRITER_INTERNAL_H_

#include <lib/zx/status.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fxt::internal {

enum class Method {
  Reserve,
  WriteWord,
  WriteBytes,
  Commit,
};

// Traits type to match valid method signatures. Base type evaluates to
// false-like.
template <Method M, typename Signature>
struct MethodTraits : std::false_type {};

// Specialization matching the required signature for Writer::Reserve. Provides
// an alias of the reservation return value type for convenience.
//
// Create a Reservation that will have `header` written to as it first 8 bytes.
template <typename Class, typename ReservationImpl>
struct MethodTraits<Method::Reserve, zx::result<ReservationImpl> (Class::*)(uint64_t header)>
    : std::true_type {
  using Reservation = ReservationImpl;
};

// Specialization matching the required signature for Reservation::WriteWord.
//
// Write a 64bit `word` into the buffer
template <typename Class>
struct MethodTraits<Method::WriteWord, void (Class::*)(uint64_t word)> : std::true_type {};

// Specialization matching the required signature for Reservation::WriteBytes.
//
// Write the `num_bytes` bytes starting at `bytes` into the buffer. If
// num_bytes is not a multiple of 8 bytes, follow with 0 padding to an 8 bytes
// boundary.
template <typename Class>
struct MethodTraits<Method::WriteBytes, void (Class::*)(const void* bytes, size_t num_bytes)>
    : std::true_type {};

// Specialization matching the required signature for Reservation::Commit.
//
// Implementation defined. The serializer calls this method when it is done
// writing into the reservation.
template <typename Class>
struct MethodTraits<Method::Commit, void (Class::*)()> : std::true_type {};

// Alias to reference MethodTraits<...>::Reservation
template <typename W>
using ReservationType = typename MethodTraits<Method::Reserve, decltype(&W::Reserve)>::Reservation;

// Detects whether the type W conforms to the requirements of a Writer type.
//
// Inherits from std::true_type when all the required Writer methods are
// present with correct signatures
template <typename W>
using WriterIsValid =
    std::conjunction<MethodTraits<Method::Reserve, decltype(&W::Reserve)>,
                     MethodTraits<Method::WriteWord, decltype(&ReservationType<W>::WriteWord)>,
                     MethodTraits<Method::WriteBytes, decltype(&ReservationType<W>::WriteBytes)>,
                     MethodTraits<Method::Commit, decltype(&ReservationType<W>::Commit)>>;

// Utility that evaluates to true-like if W conforms to the Writer protocol,
// false-like otherwise.
template <typename W, typename = void>
struct WriterTraits : std::false_type {};

// Specialization that is selected only when WriterIsValid<W> is well-formed.
// Evaluates to true-like when WriterIsValid<W> is true-like.
// Provides an alias of the Reservation type returned by zx::result<Reservation>
// W::Reserve(uint64_t).
template <typename W>
struct WriterTraits<W, std::void_t<WriterIsValid<W>>> : WriterIsValid<W> {
  using Reservation = ReservationType<W>;
};

// Enable if W is a valid Writer.
//
// To be a value writer, a type must implement the following:
//
// Have some type "Reservation" which supports:
//
//   Write a 64bit `word` into the buffer
//   > void Reservation::WriteWord(uint64_t word);
//
//   Write the `num_bytes` bytes starting at `bytes` into the buffer. If
//   num_bytes is not a multiple of 8 bytes, the writer must follow with 0
//   padding to an 8 byte boundary.
//   > void Reservation::WriteBytes(const void* bytes, size_t num_bytes);
//
//   Implementation defined. The serializer calls this method when it is done
//   writing into the reservation.
//   > void Reservation::Commit();
//
// As well as some "Writer" which supports handing out the type:
//
//   Create a Reservation that will have `header` written to as it first 8 bytes.
//   > Reservation Reserve(uint64_t header);
template <typename W>
using EnableIfWriter = std::enable_if_t<WriterTraits<W>::value, int>;
}  // namespace fxt::internal

#endif  // SRC_PERFORMANCE_LIB_FXT_INCLUDE_LIB_FXT_WRITER_INTERNAL_H_
