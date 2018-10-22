// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_CODING_TRAITS_H_
#define LIB_FIDL_CPP_CODING_TRAITS_H_

#include <lib/fidl/cpp/array.h>

#include <memory>

#include "lib/fidl/cpp/decoder.h"
#include "lib/fidl/cpp/encoder.h"
#include "lib/fidl/cpp/traits.h"
#include "lib/fidl/cpp/vector.h"

namespace fidl {

template <typename T, class Enable = void>
struct CodingTraits;

template <typename T>
struct CodingTraits<T, typename std::enable_if<IsPrimitive<T>::value>::type> {
  static constexpr size_t encoded_size = sizeof(T);
  inline static void Encode(Encoder* encoder, T* value, size_t offset) {
    *encoder->GetPtr<T>(offset) = *value;
  }
  inline static void Decode(Decoder* decoder, T* value, size_t offset) {
    *value = *decoder->GetPtr<T>(offset);
  }
};

template <>
struct CodingTraits<bool> {
  static constexpr size_t encoded_size = sizeof(bool);
  inline static void Encode(Encoder* encoder, bool* value, size_t offset) {
    *encoder->GetPtr<bool>(offset) = *value;
  }
  inline static void Encode(Encoder* encoder, std::vector<bool>::iterator value,
                            size_t offset) {
    *encoder->GetPtr<bool>(offset) = *value;
  }
  inline static void Decode(Decoder* decoder, bool* value, size_t offset) {
    *value = *decoder->GetPtr<bool>(offset);
  }
  inline static void Decode(Decoder* decoder, std::vector<bool>::iterator value,
                            size_t offset) {
    *value = *decoder->GetPtr<bool>(offset);
  }
};

#ifdef __Fuchsia__
template <typename T>
struct CodingTraits<T, typename std::enable_if<
                           std::is_base_of<zx::object_base, T>::value>::type> {
  static constexpr size_t encoded_size = sizeof(zx_handle_t);
  static void Encode(Encoder* encoder, zx::object_base* value, size_t offset) {
    encoder->EncodeHandle(value, offset);
  }
  static void Decode(Decoder* decoder, zx::object_base* value, size_t offset) {
    decoder->DecodeHandle(value, offset);
  }
};
#endif

template <typename T>
struct CodingTraits<std::unique_ptr<T>> {
  static constexpr size_t encoded_size = sizeof(uintptr_t);
  static void Encode(Encoder* encoder, std::unique_ptr<T>* value,
                     size_t offset) {
    if (value->get()) {
      *encoder->GetPtr<uintptr_t>(offset) = FIDL_ALLOC_PRESENT;
      size_t size = CodingTraits<T>::encoded_size;
      CodingTraits<T>::Encode(encoder, value->get(), encoder->Alloc(size));
    } else {
      *encoder->GetPtr<uintptr_t>(offset) = FIDL_ALLOC_ABSENT;
    }
  }
  static void Decode(Decoder* decoder, std::unique_ptr<T>* value,
                     size_t offset) {
    uintptr_t ptr = *decoder->GetPtr<uintptr_t>(offset);
    if (!ptr)
      return value->reset();
    *value = std::make_unique<T>();
    CodingTraits<T>::Decode(decoder, value->get(), decoder->GetOffset(ptr));
  }
};

void EncodeNullVector(Encoder* encoder, size_t offset);
void EncodeVectorPointer(Encoder* encoder, size_t count, size_t offset);

template <typename T>
struct CodingTraits<VectorPtr<T>> {
  static constexpr size_t encoded_size = sizeof(fidl_vector_t);
  static void Encode(Encoder* encoder, VectorPtr<T>* value, size_t offset) {
    if (value->is_null())
      return EncodeNullVector(encoder, offset);
    size_t count = (*value)->size();
    EncodeVectorPointer(encoder, count, offset);
    size_t stride = CodingTraits<T>::encoded_size;
    size_t base = encoder->Alloc(count * stride);
    for (size_t i = 0; i < count; ++i)
      CodingTraits<T>::Encode(encoder, &(*value)->at(i), base + i * stride);
  }
  static void Decode(Decoder* decoder, VectorPtr<T>* value, size_t offset) {
    fidl_vector_t* encoded = decoder->GetPtr<fidl_vector_t>(offset);
    if (!encoded->data) {
      *value = VectorPtr<T>();
      return;
    }
    value->resize(encoded->count);
    size_t stride = CodingTraits<T>::encoded_size;
    size_t base = decoder->GetOffset(encoded->data);
    size_t count = encoded->count;
    for (size_t i = 0; i < count; ++i)
      CodingTraits<T>::Decode(decoder, &(*value)->at(i), base + i * stride);
  }
};

template <typename T, size_t N>
struct CodingTraits<Array<T, N>> {
  static constexpr size_t encoded_size = CodingTraits<T>::encoded_size * N;
  static void Encode(Encoder* encoder, Array<T, N>* value, size_t offset) {
    size_t stride = CodingTraits<T>::encoded_size;
    for (size_t i = 0; i < N; ++i)
      CodingTraits<T>::Encode(encoder, &value->at(i), offset + i * stride);
  }
  static void Decode(Decoder* decoder, Array<T, N>* value, size_t offset) {
    size_t stride = CodingTraits<T>::encoded_size;
    for (size_t i = 0; i < N; ++i)
      CodingTraits<T>::Decode(decoder, &value->at(i), offset + i * stride);
  }
};

template <typename T, size_t EncodedSize>
struct EncodableCodingTraits {
  static constexpr size_t encoded_size = EncodedSize;
  static void Encode(Encoder* encoder, T* value, size_t offset) {
    value->Encode(encoder, offset);
  }
  static void Decode(Decoder* decoder, T* value, size_t offset) {
    T::Decode(decoder, value, offset);
  }
};

template <typename T>
void Encode(Encoder* encoder, T* value, size_t offset) {
  CodingTraits<T>::Encode(encoder, value, offset);
}

template <typename T>
void Decode(Decoder* decoder, T* value, size_t offset) {
  CodingTraits<T>::Decode(decoder, value, offset);
}

template <typename T>
T DecodeAs(Decoder* decoder, size_t offset) {
  T value;
  Decode(decoder, &value, offset);
  return value;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_CODING_TRAITS_H_
