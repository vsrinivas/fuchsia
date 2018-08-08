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

namespace overnet {

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
    uint8_t* (*maybe_add_prefix)(const Data* data, size_t length,
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

  const uint8_t* begin() const { return vtable_->begin(&data_); }
  const uint8_t* end() const { return vtable_->end(&data_); }
  size_t length() const { return vtable_->length(&data_); }

  void TrimBegin(size_t trim_bytes) { vtable_->trim(&data_, trim_bytes, 0); }
  void TrimEnd(size_t trim_bytes) { vtable_->trim(&data_, 0, trim_bytes); }

  Slice FromOffset(size_t offset) const {
    Slice out(*this);
    out.TrimBegin(offset);
    return out;
  }

  Slice FromPointer(const uint8_t* internal_pointer) const {
    assert(internal_pointer >= begin() && internal_pointer <= end());
    return FromOffset(internal_pointer - begin());
  }

  Slice TakeUntilOffset(size_t offset) {
    Slice out(*this);
    out.TrimEnd(length() - offset);
    TrimBegin(offset);
    return out;
  }

  Slice TakeUntilPointer(const uint8_t* internal_pointer) {
    assert(internal_pointer >= begin() && internal_pointer <= end());
    return TakeUntilOffset(internal_pointer - begin());
  }

  static Slice Join(std::initializer_list<Slice> slices) {
    return Join(slices.begin(), slices.end());
  }

  template <class IT>
  static Slice Join(IT begin, IT end) {
    if (begin == end) return Slice();
    if (std::next(begin) == end) return *begin;

    size_t total_length = 0;
    for (auto it = begin; it != end; ++it) {
      total_length += it->length();
    }

    return Slice::WithInitializer(total_length, [begin, end](uint8_t* out) {
      size_t offset = 0;
      for (auto it = begin; it != end; ++it) {
        memcpy(out + offset, it->begin(), it->length());
        offset += it->length();
      }
    });
  }

  template <class F>
  Slice WithPrefix(size_t length, F initializer) const {
    Data new_slice_data;
    if (uint8_t* prefix =
            vtable_->maybe_add_prefix(&data_, length, &new_slice_data)) {
      initializer(prefix);
      return Slice{vtable_, new_slice_data};
    } else {
      size_t own_length = this->length();
      const uint8_t* begin = this->begin();
      return WithInitializer(
          own_length + length,
          [length, own_length, initializer, begin](uint8_t* p) {
            initializer(p);
            memcpy(p + length, begin, own_length);
          });
    }
  }

  std::string AsStdString() const { return std::string(begin(), end()); }

  /////////////////////////////////////////////////////////////////////////////
  // Factory functions

  static Slice FromStaticString(const char* s) {
    struct StaticString {
      size_t length;
      const uint8_t* bytes;
    };
    auto bs = reinterpret_cast<const uint8_t*>(s);
    return Slice(&Static<>::const_vtable_, nullptr, bs, bs + strlen(s));
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
  static Slice WithInitializerAndPrefix(size_t length, size_t prefix,
                                        F&& initializer) {
    if (length <= kSmallSliceMaxLength) {
      // Ignore prefix request if this is small enough (we'll maybe allocate
      // later, but that's ok - we didn't here).
      Slice s(&Static<>::small_vtable_);
      s.data_.small.length = length;
      std::forward<F>(initializer)(s.data_.small.bytes);
      return s;
    } else {
      auto* block = BHNew(length + prefix);
      std::forward<F>(initializer)(block->bytes + prefix);
      return Slice(&Static<>::block_vtable_, block, block->bytes + prefix,
                   block->bytes + prefix + length);
    }
  }

  template <class F>
  static Slice WithInitializer(size_t length, F&& initializer) {
    return WithInitializerAndPrefix(length, 0, std::forward<F>(initializer));
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
  static uint8_t* SmallAddPrefix(const Data* data, size_t length,
                                 Data* new_slice_data) {
    if (data->small.length + length < kSmallSliceMaxLength) {
      new_slice_data->small.length = data->small.length + length;
      memcpy(new_slice_data->small.bytes + length, data->small.bytes,
             data->small.length);
      return new_slice_data->small.bytes;
    }
    return nullptr;
  }

  struct BlockHeader {
    int refs;
    uint8_t bytes[0];
  };
  static BlockHeader* BHNew(size_t length) {
    auto* out = static_cast<BlockHeader*>(malloc(sizeof(BlockHeader) + length));
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
  static uint8_t* BHAddPrefix(const Data* data, size_t length,
                              Data* new_slice_data) {
    auto* hdr = static_cast<BlockHeader*>(data->general.control);
    if (hdr->refs != 1) return nullptr;
    assert(data->general.begin - hdr->bytes >= 0);
    if (static_cast<size_t>(data->general.begin - hdr->bytes) >= length) {
      *new_slice_data =
          Data(hdr, data->general.begin - length, data->general.end);
      return const_cast<uint8_t*>(new_slice_data->general.begin);
    }
    return nullptr;
  }

  static uint8_t* NoAddPrefix(const Data* data, size_t length,
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
    // maybe_add_prefix
    SmallAddPrefix,
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
    // maybe_add_prefix
    NoAddPrefix,
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
    // maybe_add_prefix
    BHAddPrefix,
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
    if (trim_bytes != 0) end_of_message = false;
  }

  Chunk TakeUntilSliceOffset(size_t slice_offset) {
    Chunk out{offset, false, slice.TakeUntilOffset(slice_offset)};
    offset += slice_offset;
    return out;
  }
};

inline bool operator==(const Slice& a, const Slice& b) {
  if (a.length() != b.length()) return false;
  return 0 == memcmp(a.begin(), b.begin(), a.length());
}

inline bool operator==(const Chunk& a, const Chunk& b) {
  return a.offset == b.offset && a.slice == b.slice;
}

std::ostream& operator<<(std::ostream& out, const Slice& slice);
std::ostream& operator<<(std::ostream& out, const Chunk& chunk);

}  // namespace overnet
