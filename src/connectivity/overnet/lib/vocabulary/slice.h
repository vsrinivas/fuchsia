// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <iosfwd>
#include <string>
#include <type_traits>
#include <vector>
#include "src/connectivity/overnet/lib/vocabulary/optional.h"

namespace overnet {

// Describes the extent of padding both before and after a slice.
struct Border {
  size_t prefix;
  size_t suffix;

  static Border Prefix(size_t size) { return Border{size, 0}; }
  static Border Suffix(size_t size) { return Border{0, size}; }
  static Border None() { return Border{0, 0}; }

  Border WithAddedPrefix(size_t size) const {
    return Border{prefix + size, suffix};
  }

  Border WithAddedSuffix(size_t size) const {
    return Border{size, suffix + size};
  }

  size_t Total() const { return prefix + suffix; }
};

class Slice final {
 public:
  static constexpr size_t kSmallSliceMaxLength = 3 * sizeof(void*) - 1;
  union Data {
    Data() {}
    Data(void* control, const uint8_t* begin, const uint8_t* end)
        : general{control, begin, end} {}
    struct {
      void* control;
      const uint8_t* begin;
      const uint8_t* end;
    } general;
    struct {
      uint8_t length;
      uint8_t bytes[kSmallSliceMaxLength];
    } small;
  };

  struct VTable {
    const uint8_t* (*begin)(const Data* data);
    const uint8_t* (*end)(const Data* data);
    size_t (*length)(const Data* data);
    void (*ref)(Data* data);
    void (*unref)(Data* data);
    void (*trim)(Data* data, size_t trim_left, size_t trim_right);
    uint8_t* (*maybe_add_borders)(const Data* data, Border border,
                                  Data* new_slice_data);
    const char* name;
  };

  Slice(const VTable* vtable, void* control, const uint8_t* begin,
        const uint8_t* end)
      : vtable_(vtable), data_(control, begin, end) {}

  Slice() : Slice(&Static<>::small_vtable_) { data_.small.length = 0; }

  ~Slice() { vtable_->unref(&data_); }

  Slice(const Slice& other) : vtable_(other.vtable_), data_(other.data_) {
    vtable_->ref(&data_);
  }

  Slice(Slice&& other) : vtable_(other.vtable_), data_(other.data_) {
    other.vtable_ = &Static<>::small_vtable_;
    other.data_.small.length = 0;
  }

  Slice& operator=(const Slice& other) {
    Slice(other).Swap(this);
    return *this;
  }

  Slice& operator=(Slice&& other) {
    Swap(&other);
    return *this;
  }

  void Swap(Slice* other) {
    std::swap(vtable_, other->vtable_);
    std::swap(data_, other->data_);
  }

  static Optional<Slice> JoinIfSameUnderlyingMemory(const Slice& a,
                                                    const Slice& b) {
    if (a.vtable_ != b.vtable_) {
      return Nothing;
    }

    if (a.end() != b.begin()) {
      return Nothing;
    }

    if (a.data_.general.control != b.data_.general.control) {
      return Nothing;
    }

    Slice out(a.vtable_, a.data_.general.control, a.begin(), b.end());
    out.vtable_->ref(&out.data_);
    return out;
  }

  const uint8_t* begin() const { return vtable_->begin(&data_); }
  const uint8_t* end() const { return vtable_->end(&data_); }
  size_t length() const { return vtable_->length(&data_); }

  void Trim(size_t left, size_t right) { vtable_->trim(&data_, left, right); }
  void TrimBegin(size_t trim_bytes) { Trim(trim_bytes, 0); }
  void TrimEnd(size_t trim_bytes) { Trim(0, trim_bytes); }

  Slice FromOffset(size_t offset) const {
    Slice out(*this);
    out.TrimBegin(offset);
    return out;
  }

  Slice ToOffset(size_t offset) const {
    Slice out(*this);
    out.TrimEnd(length() - offset);
    return out;
  }

  Slice FromPointer(const uint8_t* internal_pointer) const {
    assert(internal_pointer >= begin() && internal_pointer <= end());
    return FromOffset(internal_pointer - begin());
  }

  Slice TakeUntilOffset(size_t offset) {
    auto out = ToOffset(offset);
    TrimBegin(offset);
    return out;
  }

  Slice TakeUntilPointer(const uint8_t* internal_pointer) {
    assert(internal_pointer >= begin() && internal_pointer <= end());
    return TakeUntilOffset(internal_pointer - begin());
  }

  Slice TakeFromOffset(size_t offset) {
    auto out = FromOffset(offset);
    TrimEnd(length() - offset);
    return out;
  }

  Slice Cut(size_t from_offset, size_t to_offset) {
    Slice copy(*this);
    copy.Trim(from_offset, copy.length() - to_offset);
    return copy;
  }

  static Slice Join(std::initializer_list<Slice> slices,
                    Border desired_border = Border::None()) {
    return Join(slices.begin(), slices.end(), desired_border);
  }

  template <class IT>
  static Slice Join(IT begin, IT end, Border desired_border = Border::None()) {
    if (begin == end) {
      return Slice();
    }

    if (std::next(begin) == end) {
      return *begin;
    }

    size_t total_length = 0;
    for (auto it = begin; it != end; ++it) {
      total_length += it->length();
    }

    return Slice::WithInitializerAndBorders(
        total_length, desired_border, [begin, end](uint8_t* out) {
          size_t offset = 0;
          for (auto it = begin; it != end; ++it) {
            memcpy(out + offset, it->begin(), it->length());
            offset += it->length();
          }
        });
  }

  void Append(Slice slice) {
    if (length() == 0) {
      *this = std::move(slice);
    } else if (slice.length() == 0) {
      return;
    } else {
      *this = Join({std::move(*this), std::move(slice)});
    }
  }

  template <class IT>
  static Slice AlignedJoin(IT begin, IT end) {
    if (begin == end) {
      return Slice();
    }

    if (std::next(begin) == end && IsAligned(begin->begin())) {
      return *begin;
    }

    size_t total_length = 0;
    for (auto it = begin; it != end; ++it) {
      total_length += it->length();
    }

    return Slice::WithInitializerAndBorders(
        total_length, Border::None(), [begin, end](uint8_t* out) {
          assert(IsAligned(out));
          size_t offset = 0;
          for (auto it = begin; it != end; ++it) {
            memcpy(out + offset, it->begin(), it->length());
            offset += it->length();
          }
        });
  }

  static Slice Aligned(Slice input) {
    const uint8_t* begin = input.begin();
    if (IsAligned(begin)) {
      return input;
    }
    const size_t length = input.length();
    return Slice::WithInitializerAndBorders(length, Border::None(),
                                            [begin, length](uint8_t* out) {
                                              assert(IsAligned(out));
                                              memcpy(out, begin, length);
                                            });
  }

  template <class F>
  Slice WithPrefix(size_t length, F initializer) const {
    return WithBorders(Border::Prefix(length), initializer);
  }

  template <class F>
  Slice WithBorders(Border border, F initializer) const {
    Data new_slice_data;
    if (uint8_t* prefix =
            vtable_->maybe_add_borders(&data_, border, &new_slice_data)) {
      initializer(prefix);
      return Slice{vtable_, new_slice_data};
    } else {
      size_t own_length = this->length();
      const uint8_t* begin = this->begin();
      return WithInitializer(own_length + border.prefix + border.suffix,
                             [add_prefix = border.prefix, own_length,
                              initializer, begin](uint8_t* p) {
                               memcpy(p + add_prefix, begin, own_length);
                               initializer(p);
                             });
    }
  }

  template <class F>
  Slice MutateUnique(F f) const {
    return WithBorders(Border::None(), f);
  }

  std::string AsStdString() const { return std::string(begin(), end()); }

  bool StartsWith(const Slice& prefix) const;

  /////////////////////////////////////////////////////////////////////////////
  // Factory functions

  static Slice FromStaticString(const char* s) {
    auto bs = reinterpret_cast<const uint8_t*>(s);
    return Slice(&Static<>::const_vtable_, nullptr, bs, bs + strlen(s));
  }

  template <class I>
  static Slice ReferencingContainer(I begin, I end) {
    return Slice(&Static<>::const_vtable_, nullptr,
                 static_cast<const uint8_t*>(begin),
                 static_cast<const uint8_t*>(end));
  }

  static Slice FromCopiedBuffer(const void* data, size_t length) {
    return WithInitializer(
        length, [data, length](void* p) { memcpy(p, data, length); });
  }

  template <class C>
  static Slice FromContainer(const C& c) {
    auto begin = c.begin();
    auto end = c.end();
    return WithInitializer(end - begin, [begin, end](uint8_t* p) {
      for (auto i = begin; i != end; ++i) {
        *p++ = *i;
      }
    });
  }

  static Slice FromContainer(std::initializer_list<uint8_t> c) {
    auto begin = c.begin();
    auto end = c.end();
    return WithInitializer(end - begin, [begin, end](uint8_t* p) {
      for (auto i = begin; i != end; ++i) {
        *p++ = *i;
      }
    });
  }

  template <class F>
  static Slice WithInitializerAndBorders(size_t length, Border border,
                                         F&& initializer) {
    if (length <= kSmallSliceMaxLength) {
      // Ignore prefix/suffix request if this is small enough (we'll maybe
      // allocate later, but that's ok - we didn't here).
      Slice s(&Static<>::small_vtable_);
      s.data_.small.length = length;
      std::forward<F>(initializer)(s.data_.small.bytes);
      return s;
    } else {
      auto* block = BHNew(length + border.prefix + border.suffix);
      std::forward<F>(initializer)(block->bytes + border.prefix);
      return Slice(&Static<>::block_vtable_, block,
                   block->bytes + border.prefix,
                   block->bytes + border.prefix + length);
    }
  }

  template <class F>
  static Slice WithInitializer(size_t length, F&& initializer) {
    return WithInitializerAndBorders(length, Border::None(),
                                     std::forward<F>(initializer));
  }

  static Slice RepeatedChar(size_t count, char c) {
    return WithInitializer(count, [count, c](uint8_t* p) {
      for (size_t i = 0; i < count; i++) {
        p[i] = static_cast<uint8_t>(c);
      }
    });
  }

  // Given an object that conforms to the Writer interface (has size_t
  // wire_length() and Write(uint8_t* out)), create a slice containing the
  // serialized object
  template <class... W>
  static Slice FromWriters(const W&... w) {
    uint64_t total_length = 0;
    (void)std::initializer_list<int>{(total_length += w.wire_length(), 0)...};
    return WithInitializer(total_length, [&w...](uint8_t* bytes) {
      uint8_t* p = bytes;
      (void)std::initializer_list<int>{(p = w.Write(p), 0)...};
    });
  }

  // Given an object of type T that has a T::Writer interface, generate a slice
  template <class T>
  static Slice FromWritable(const T& t) {
    typename T::Writer writer(&t);
    return FromWriters(writer);
  }

 private:
  // Leaves data_ uninitialized
  Slice(const VTable* vtable) : vtable_(vtable) {}

  Slice(const VTable* vtable, Data data) : vtable_(vtable), data_(data) {}

  const VTable* vtable_;
  Data data_;

  static bool IsAligned(const uint8_t* ptr) {
    auto uintptr = reinterpret_cast<uintptr_t>(ptr);
    constexpr uintptr_t kAlignment = 8;
    return uintptr % kAlignment == 0;
  }

  static void NoOpRef(Data*) {}

  static const uint8_t* GeneralBegin(const Data* data) {
    return data->general.begin;
  }
  static const uint8_t* GeneralEnd(const Data* data) {
    return data->general.end;
  }
  static size_t GeneralLength(const Data* data) {
    return data->general.end - data->general.begin;
  }
  static void GeneralTrim(Data* data, size_t trim_left, size_t trim_right) {
    data->general.begin += trim_left;
    data->general.end -= trim_right;
  }

  static const uint8_t* SmallBegin(const Data* data) {
    return data->small.bytes;
  }
  static const uint8_t* SmallEnd(const Data* data) {
    return data->small.bytes + data->small.length;
  }
  static size_t SmallLength(const Data* data) { return data->small.length; }
  static void SmallTrim(Data* data, size_t trim_left, size_t trim_right) {
    data->small.length -= trim_right + trim_left;
    if (trim_left) {
      memmove(data->small.bytes, data->small.bytes + trim_left,
              data->small.length);
    }
  }
  static uint8_t* SmallAddBorders(const Data* data, Border border,
                                  Data* new_slice_data) {
    if (data->small.length + border.prefix + border.suffix <
        kSmallSliceMaxLength) {
      new_slice_data->small.length =
          data->small.length + border.prefix + border.suffix;
      memcpy(new_slice_data->small.bytes + border.prefix, data->small.bytes,
             data->small.length);
      return new_slice_data->small.bytes;
    }
    return nullptr;
  }

  struct BlockHeader {
    int refs;
    size_t block_length;
    uint8_t bytes[0];
  };
  static BlockHeader* BHNew(size_t length) {
    auto* out = static_cast<BlockHeader*>(malloc(sizeof(BlockHeader) + length));
    out->block_length = length;
    out->refs = 1;
    return out;
  }
  static void BHRef(Data* data) {
    static_cast<BlockHeader*>(data->general.control)->refs++;
  }
  static void BHUnref(Data* data) {
    if (0 == --static_cast<BlockHeader*>(data->general.control)->refs) {
      free(data->general.control);
    }
  }
  static uint8_t* BHAddBorders(const Data* data, Border border,
                               Data* new_slice_data) {
    auto* hdr = static_cast<BlockHeader*>(data->general.control);
    if (hdr->refs != 1)
      return nullptr;
    assert(data->general.begin - hdr->bytes >= 0);
    if (static_cast<size_t>(data->general.begin - hdr->bytes) >=
            border.prefix &&
        static_cast<size_t>(hdr->bytes + hdr->block_length -
                            data->general.end) >= border.suffix) {
      hdr->refs++;
      *new_slice_data = Data(hdr, data->general.begin - border.prefix,
                             data->general.end + border.suffix);
      return const_cast<uint8_t*>(new_slice_data->general.begin);
    }
    return nullptr;
  }

  static uint8_t* NoAddBorders(const Data* data, Border border,
                               Data* new_slice_data) {
    return nullptr;
  }

  template <int I = 0>
  struct Static {
    static const VTable small_vtable_;
    static const VTable const_vtable_;
    static const VTable block_vtable_;
  };
};

template <int I>
const Slice::VTable Slice::Static<I>::small_vtable_ = {
    // begin
    SmallBegin,
    // end
    SmallEnd,
    // length
    SmallLength,
    // ref
    NoOpRef,
    // unref
    NoOpRef,
    // trim
    SmallTrim,
    // maybe_add_borders
    SmallAddBorders,
    // name
    "small_vtable"};

template <int I>
const Slice::VTable Slice::Static<I>::const_vtable_ = {
    // begin
    GeneralBegin,
    // end
    GeneralEnd,
    // length
    GeneralLength,
    // ref
    NoOpRef,
    // unref
    NoOpRef,
    // trim
    GeneralTrim,
    // maybe_add_borders
    NoAddBorders,
    // name
    "small_vtable"};

template <int I>
const Slice::VTable Slice::Static<I>::block_vtable_ = {
    // begin
    GeneralBegin,
    // end
    GeneralEnd,
    // length
    GeneralLength,
    // ref
    BHRef,
    // unref
    BHUnref,
    // trim
    GeneralTrim,
    // maybe_add_borders
    BHAddBorders,
    // name
    "block_vtable"};

struct Chunk final {
  uint64_t offset;
  bool end_of_message;
  Slice slice;

  void TrimBegin(size_t trim_bytes) {
    slice.TrimBegin(trim_bytes);
    offset += trim_bytes;
  }

  void TrimEnd(size_t trim_bytes) {
    slice.TrimEnd(trim_bytes);
    if (trim_bytes != 0)
      end_of_message = false;
  }

  Chunk TakeUntilSliceOffset(size_t slice_offset) {
    Chunk out{offset, false, slice.TakeUntilOffset(slice_offset)};
    offset += slice_offset;
    return out;
  }

  Chunk TakeFromSliceOffset(size_t slice_offset) {
    Chunk out{offset + slice_offset, end_of_message,
              slice.TakeFromOffset(slice_offset)};
    end_of_message = false;
    return out;
  }

  static Optional<Chunk> JoinIfSameUnderlyingMemory(const Chunk& a,
                                                    const Chunk& b) {
    if (a.offset + a.slice.length() != b.offset) {
      return Nothing;
    }

    return Slice::JoinIfSameUnderlyingMemory(a.slice, b.slice)
        .Map([offset = a.offset,
              end_of_message = b.end_of_message](Slice slice) {
          return Chunk{offset, end_of_message, std::move(slice)};
        });
  }
};

inline bool operator==(const Slice& a, const Slice& b) {
  if (a.length() != b.length())
    return false;
  return 0 == memcmp(a.begin(), b.begin(), a.length());
}

inline bool operator!=(const Slice& a, const Slice& b) { return !(a == b); }

inline bool operator==(const Chunk& a, const Chunk& b) {
  return a.offset == b.offset && a.slice == b.slice;
}

inline bool Slice::StartsWith(const Slice& prefix) const {
  if (length() < prefix.length()) {
    return false;
  }
  return ToOffset(prefix.length()) == prefix;
}

std::ostream& operator<<(std::ostream& out, const Border& border);
std::ostream& operator<<(std::ostream& out, const Slice& slice);
std::ostream& operator<<(std::ostream& out, const std::vector<Slice>& slices);
std::ostream& operator<<(std::ostream& out, const Chunk& chunk);

}  // namespace overnet
