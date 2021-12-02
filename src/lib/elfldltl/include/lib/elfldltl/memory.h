// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MEMORY_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MEMORY_H_

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <optional>

namespace elfldltl {

// Various interfaces require a File or Memory type to access data structures.
//
// This header specifies the API contracts those template interfaces require,
// and provides an implementation for the simplest case.
//
// Both File and Memory types are not copied or moved, only used by reference.
// Each interface uses either the File API or the Memory API, but both APIs
// can be implemented by a single object when appropriate.
//
// The File type provides these methods, which take an offset into the file,
// guaranteed to be correctly aligned with respect to T:
//
//  * template <typename T>
//    std::optional<Result> ReadFromFile(size_t offset);
//
//    This reads a single datum from the file.  If Result is not T then const
//    Result& is convertible to const T&.  Thus Result can yield the T by value
//    or by reference, depending on the implementation.  In the simple memory
//    implementation it is by reference.  Other implementations read directly
//    into a local T object and return that.
//
//  * template <typename T, size_t MaxCount>
//    std::optional<Result> ReadArrayFromFile(size_t offset, size_t count);
//
//   This is like ReadFromFile, but for an array of T[count].  The const
//   Result& referring to the return value's value() can be treated as a
//   view-like container of T whose size() == count, but that might own the
//   data.  MaxCount may be ignored if Result is a non-owning type.  If the
//   array will be returned by value, MaxCount is a reasonable static upper
//   bound for count.  A count beyond the static limit results in std::nullopt,
//   not truncation.
//
// The Memory type provides these methods, which take a memory address as used
// in the ELF metadata in this file, guaranteed to be correctly aligned with
// respect to T.
//
//  * template <typename T>
//    std::optional<cpp20::span<const T>> ReadArray(uintptr_t address, size_t count);
//
//   This returns a view of T[count] if that's accessible at the address.  The
//   data must be permanently accessible for the lifetime of the Memory object.
//
//  * template <typename T>
//    std::optional<cpp20::span<const T>> ReadArray(uintptr_t address);
//
//   This is the same but for when the caller doesn't know the size of the
//   array.  So this returns a view of T[n] for some n > 0 that is accessible,
//   as much as is possibly accessible for valid RODATA in the ELF file's
//   memory image.  The caller will be doing random-access that will only
//   access the "actually valid" indices of the returned span if the rest of
//   the input data (e.g. relocation records) is also valid.  Access past the
//   true size of the array may return garbage, but reading from pointers into
//   anywhere in the span returned will at least be safe to perform (for the
//   lifetime of the Memory object).
//
//  * template <typename T>
//    bool Store(Addr address, U value);
//
//    This stores a T at the given address, which is in some writable segment
//    of the file previously arranged with this Memory object.  It returns
//    false if processing should fail early.  Note the explicit template
//    argument is always used to indicate the type whose operator= will be
//    called on the actual memory, so it is of the explicitly intended width
//    and can be a byte-swapping type.  The value argument might be of the same
//    type or of any type convertible to it.
//
//  * template <typename T>
//    bool StoreAdd(Addr address, U value);
//
//    This is like Store but it adds the argument to the word already in place,
//    i.e. the in-place addend.  Note T::operator= is always called, not +=.

// This does direct memory access to an ELF load image already mapped in.
// Addresses in the ELF metadata are relative to a given base address that
// corresponds to the beginning of the image this object points to.
class DirectMemory {
 public:
  // This type could easily be copyable.  But the template APIs should always
  // use Memory types only be reference.  So make this type uncopyable and
  // unmovable just so using it enforces API constraints other implementations
  // might actually need to rely on.
  DirectMemory() = delete;
  DirectMemory(const DirectMemory&) = delete;
  DirectMemory(DirectMemory&&) = delete;

  // This takes a memory image and the file-relative address it corresponds to.
  DirectMemory(cpp20::span<std::byte> image, uintptr_t base) : image_(image), base_(base) {}

  // File API assumes this file's first segment has page-aligned p_offset of 0.

  template <typename T>
  std::optional<std::reference_wrapper<const T>> ReadFromFile(size_t offset) {
    if (offset >= image_.size()) [[unlikely]] {
      return std::nullopt;
    }
    auto memory = image_.subspan(offset);
    if (memory.size() < sizeof(T)) [[unlikely]] {
      return std::nullopt;
    }
    return std::cref(*reinterpret_cast<const T*>(memory.data()));
  }

  template <typename T, size_t MaxCount>
  std::optional<cpp20::span<const T>> ReadArrayFromFile(size_t offset, size_t count) {
    auto data = ReadAll<T>(offset);
    if (data.empty() || count > data.size()) [[unlikely]] {
      return std::nullopt;
    }
    return data.subspan(0, count);
  }

  // Memory API assumes the image represents the PT_LOAD segment layout of the
  // file by p_vaddr relative to the base address (not the raw file image by
  // p_offset).

  template <typename T>
  std::optional<cpp20::span<const T>> ReadArray(uintptr_t ptr, size_t count) {
    if (ptr < base_) [[unlikely]] {
      return std::nullopt;
    }
    return ReadArrayFromFile<T, 0>(ptr - base_, count);
  }

  template <typename T>
  std::optional<cpp20::span<const T>> ReadArray(uintptr_t ptr) {
    if (ptr < base_) [[unlikely]] {
      return std::nullopt;
    }
    auto data = ReadAll<T>(ptr - base_);
    if (data.empty()) [[unlikely]] {
      return std::nullopt;
    }
    return data;
  }

  // Note the argument is not of type T so that T can never be deduced from the
  // argument: the caller must use Store<T> explicitly to avoid accidentally
  // using the wrong type since lots of integer types are silently coercible to
  // other ones.  (The caller doesn't need to supply the U template parameter.)
  template <typename T, typename U>
  bool Store(uintptr_t ptr, U value) {
    if (auto word = StoreLocation<T>(ptr)) [[likely]] {
      *word = value;
      return true;
    }
    return false;
  }

  // Note the argument is not of type T so that T can never be deduced from the
  // argument: the caller must use Store<T> explicitly to avoid accidentally
  // using the wrong type since lots of integer types are silently coercible to
  // other ones.  (The caller doesn't need to supply the U template parameter.)
  template <typename T, typename U>
  bool StoreAdd(uintptr_t ptr, U value) {
    if (auto word = StoreLocation<T>(ptr)) [[likely]] {
      *word = *word + value;  // Don't assume T::operator+= works.
      return true;
    }
    return false;
  }

 private:
  template <typename T>
  cpp20::span<const T> ReadAll(size_t offset) {
    if (offset >= image_.size()) [[unlikely]] {
      return {};
    }
    auto memory = image_.subspan(offset);
    return {
        reinterpret_cast<const T*>(memory.data()),
        memory.size() / sizeof(T),
    };
  }

  template <typename T>
  T* StoreLocation(uintptr_t ptr) {
    if (ptr < base_ || ptr - base_ >= image_.size() || image_.size() - (ptr - base_) < sizeof(T))
        [[unlikely]] {
      return nullptr;
    }
    return reinterpret_cast<T*>(&image_[ptr - base_]);
  }

  cpp20::span<std::byte> image_;
  uintptr_t base_;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MEMORY_H_
