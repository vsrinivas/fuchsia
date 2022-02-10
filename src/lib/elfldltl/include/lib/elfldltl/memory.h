// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MEMORY_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MEMORY_H_

#include <lib/stdcompat/span.h>

#include <cassert>
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
//  * template <typename T, typename Allocator>
//    std::optional<Result> ReadArrayFromFile(size_t offset,
//                                            Allocator&& allocator,
//                                            size_t count);
//
//   This is like ReadFromFile, but for an array of T[count].  The const
//   Result& referring to the return value's value() is implicitly convertible
//   to cpp20::span<T>, but it might own the data.  Any particular File
//   implementation is free to ignore `allocator` instead always return its own
//   result type that may or may not be an owning type.
//
// The Memory type provides these methods, which take a memory address as used
// in the ELF metadata in this file, guaranteed to be correctly aligned with
// respect to T.
//
//  * template <typename T>
//    std::optional<cpp20::span<T>> ReadArray(uintptr_t address, size_t count);
//
//   This returns a view of T[count] if that's accessible at the address.  The
//   data must be permanently accessible for the lifetime of the Memory object.
//
//  * template <typename T>
//    std::optional<cpp20::span<T>> ReadArray(uintptr_t address);
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

// elfldltl::DirectMemory::ReadArrayFromFile ignores its Allocator argument,
// but other implementations need one.  A few common convenience Allocator
// implementations are provided here.

// This is the stub implementation of the Allocator API that can be used with
// DirectMemory or other implementations that never call it.
template <typename T>
struct NoArrayFromFile {
  using Result = cpp20::span<T>;

  std::optional<Result> operator()(size_t size) const { return std::nullopt; }
};

// This is an implementation of the Allocator API for File::ReadArrayFromFile
// that uses plain `new T[count]`.  Its return value object owns the data via
// `std::unique_ptr<T[]>`.
template <typename T>
class NewArrayFromFile {
 public:
  class Result {
   public:
    constexpr Result() = default;
    Result(const Result&) = delete;
    constexpr Result(Result&&) noexcept = default;

    constexpr explicit Result(std::unique_ptr<T[]> ptr, size_t size)
        : ptr_(std::move(ptr)), size_(size) {}

    Result& operator=(const Result&) noexcept = delete;
    constexpr Result& operator=(Result&&) noexcept = default;

    constexpr cpp20::span<T> get() const { return {ptr_.get(), size_}; }

    constexpr cpp20::span<T> release() { return {ptr_.release(), size_}; }

    constexpr operator bool() const { return ptr_; }

    constexpr operator cpp20::span<T>() const { return get(); }

   private:
    std::unique_ptr<T[]> ptr_;
    size_t size_ = 0;
  };

  std::optional<Result> operator()(size_t size) const {
    // Uninitialized a la C++20 std::make_unique_for_overwrite.
    if (T* ptr = new T[size]) [[likely]] {
      return Result{std::unique_ptr<T[]>(ptr), size};
    }
    return std::nullopt;
  }
};

// This is an implementation of the Allocator API for File::ReadArrayFromFile
// that uses a fixed buffer inside the object (i.e. on the stack).  It simply
// fails if more than MaxCount elements need to be read.
template <typename T, size_t MaxCount>
class FixedArrayFromFile {
 public:
  class Result {
   public:
    // For consistency with the minimal API requirement, this is move-only.
    constexpr Result() = default;
    Result(const Result&) = delete;
    constexpr Result(Result&&) noexcept = default;

    constexpr explicit Result(size_t size) : size_(size) {
      // Note the data_ elements are left uninitialized.
      assert(size_ <= std::size(data_));
    }

    Result& operator=(const Result&) noexcept = delete;
    constexpr Result& operator=(Result&&) noexcept = default;

    constexpr operator cpp20::span<T>() { return cpp20::span(data_).subspan(0, size_); }

    constexpr operator bool() const { return size_ > 0; }

   private:
    std::array<T, MaxCount> data_;
    size_t size_ = 0;
  };

  std::optional<Result> operator()(size_t size) const {
    if (size > MaxCount) {
      return std::nullopt;
    }
    return Result{size};
  }
};

// This does direct memory access to an ELF load image already mapped in.
// Addresses in the ELF metadata are relative to a given base address that
// corresponds to the beginning of the image this object points to.
class DirectMemory {
 public:
  DirectMemory() = default;

  // This type could easily be copyable.  But the template APIs should always
  // use Memory types only be reference.  So make this type uncopyable and
  // unmovable just so using it enforces API constraints other implementations
  // might actually need to rely on.
  DirectMemory(const DirectMemory&) = delete;
  DirectMemory(DirectMemory&&) = delete;

  // This takes a memory image and the file-relative address it corresponds to.
  // The one-argument form can be used to use the File API before the base is
  // known.  Then set_base must be called before using the Memory API.
  explicit DirectMemory(cpp20::span<std::byte> image, uintptr_t base = ~uintptr_t{})
      : image_(image), base_(base) {}

  cpp20::span<std::byte> image() const { return image_; }
  void set_image(cpp20::span<std::byte> image) { image_ = image; }

  uintptr_t base() const { return base_; }
  void set_base(uintptr_t base) { base_ = base; }

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

  template <typename T, typename Allocator>
  std::optional<cpp20::span<const T>> ReadArrayFromFile(size_t offset, Allocator&& allocator,
                                                        size_t count) {
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
    return ReadArrayFromFile<T>(ptr - base_, NoArrayFromFile<T>(), count);
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
  uintptr_t base_ = 0;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_MEMORY_H_
