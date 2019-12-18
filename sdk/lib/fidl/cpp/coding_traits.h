// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_CODING_TRAITS_H_
#define LIB_FIDL_CPP_CODING_TRAITS_H_

#include <array>
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
  static constexpr size_t inline_size_v1_no_ee = sizeof(T);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, T* value, size_t offset) {
    *encoder->template GetPtr<T>(offset) = *value;
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, T* value, size_t offset) {
    *value = *decoder->template GetPtr<T>(offset);
  }
};

template <>
struct CodingTraits<bool> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(bool);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, bool* value, size_t offset) {
    *encoder->template GetPtr<bool>(offset) = *value;
  }
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::vector<bool>::iterator value, size_t offset) {
    *encoder->template GetPtr<bool>(offset) = *value;
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, bool* value, size_t offset) {
    *value = *decoder->template GetPtr<bool>(offset);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, std::vector<bool>::iterator value, size_t offset) {
    *value = *decoder->template GetPtr<bool>(offset);
  }
};

#ifdef __Fuchsia__
template <typename T>
struct CodingTraits<T, typename std::enable_if<std::is_base_of<zx::object_base, T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(zx_handle_t);
  static void Encode(Encoder* encoder, zx::object_base* value, size_t offset) {
    encoder->EncodeHandle(value, offset);
  }
  static void Decode(Decoder* decoder, zx::object_base* value, size_t offset) {
    decoder->DecodeHandle(value, offset);
  }
};
#endif

template <typename T>
struct CodingTraits<std::unique_ptr<T>, typename std::enable_if<!IsFidlXUnion<T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(uintptr_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::unique_ptr<T>* value, size_t offset) {
    if (value->get()) {
      *encoder->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_PRESENT;
      CodingTraits<T>::Encode(encoder, value->get(),
                              encoder->Alloc(CodingTraits<T>::inline_size_v1_no_ee));
    } else {
      *encoder->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_ABSENT;
    }
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, std::unique_ptr<T>* value, size_t offset) {
    uintptr_t ptr = *decoder->template GetPtr<uintptr_t>(offset);
    if (!ptr)
      return value->reset();
    *value = std::make_unique<T>();
    CodingTraits<T>::Decode(decoder, value->get(), decoder->GetOffset(ptr));
  }
};

template <class EncoderImpl>
void EncodeNullVector(EncoderImpl* encoder, size_t offset) {
  fidl_vector_t* vector = encoder->template GetPtr<fidl_vector_t>(offset);
  vector->count = 0u;
  vector->data = reinterpret_cast<void*>(FIDL_ALLOC_ABSENT);
}

template <class EncoderImpl>
void EncodeVectorPointer(EncoderImpl* encoder, size_t count, size_t offset) {
  fidl_vector_t* vector = encoder->template GetPtr<fidl_vector_t>(offset);
  vector->count = count;
  vector->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
}

template <typename T>
struct CodingTraits<VectorPtr<T>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, VectorPtr<T>* value, size_t offset) {
    if (!value->has_value())
      return EncodeNullVector(encoder, offset);
    std::vector<T>& vector = **value;
    CodingTraits<::std::vector<T>>::Encode(encoder, &vector, offset);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, VectorPtr<T>* value, size_t offset) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);
    if (!encoded->data) {
      *value = VectorPtr<T>();
      return;
    }
    std::vector<T> vector;
    CodingTraits<std::vector<T>>::Decode(decoder, &vector, offset);
    (*value) = std::move(vector);
  }
};

template <typename T>
struct CodingTraits<::std::vector<T>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, ::std::vector<T>* value, size_t offset) {
    size_t count = value->size();
    EncodeVectorPointer(encoder, count, offset);
    size_t stride = CodingTraits<T>::inline_size_v1_no_ee;
    size_t base = encoder->Alloc(count * stride);
    for (size_t i = 0; i < count; ++i)
      CodingTraits<T>::Encode(encoder, &value->at(i), base + i * stride);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, ::std::vector<T>* value, size_t offset) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);
    value->resize(encoded->count);
    size_t stride = CodingTraits<T>::inline_size_v1_no_ee;
    size_t base = decoder->GetOffset(encoded->data);
    size_t count = encoded->count;
    for (size_t i = 0; i < count; ++i)
      CodingTraits<T>::Decode(decoder, &value->at(i), base + i * stride);
  }
};

template <typename T, size_t N>
struct CodingTraits<::std::array<T, N>> {
  static constexpr size_t inline_size_v1_no_ee = CodingTraits<T>::inline_size_v1_no_ee * N;
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::array<T, N>* value, size_t offset) {
    size_t stride;
    stride = CodingTraits<T>::inline_size_v1_no_ee;
    for (size_t i = 0; i < N; ++i)
      CodingTraits<T>::Encode(encoder, &value->at(i), offset + i * stride);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, std::array<T, N>* value, size_t offset) {
    size_t stride = CodingTraits<T>::inline_size_v1_no_ee;
    for (size_t i = 0; i < N; ++i)
      CodingTraits<T>::Decode(decoder, &value->at(i), offset + i * stride);
  }
};

template <typename T, size_t InlineSizeV1NoEE>
struct EncodableCodingTraits {
  static constexpr size_t inline_size_v1_no_ee = InlineSizeV1NoEE;
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, T* value, size_t offset) {
    value->Encode(encoder, offset);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, T* value, size_t offset) {
    T::Decode(decoder, value, offset);
  }
};

template <typename T, class EncoderImpl = Encoder>
size_t EncodingInlineSize(EncoderImpl* encoder) {
  return CodingTraits<T>::inline_size_v1_no_ee;
}

template <typename T, class DecoderImpl = Decoder>
size_t DecodingInlineSize(DecoderImpl* decoder) {
  return CodingTraits<T>::inline_size_v1_no_ee;
}

template <typename T, class EncoderImpl>
void Encode(EncoderImpl* encoder, T* value, size_t offset) {
  CodingTraits<T>::Encode(encoder, value, offset);
}

template <typename T, class DecoderImpl>
void Decode(DecoderImpl* decoder, T* value, size_t offset) {
  CodingTraits<T>::Decode(decoder, value, offset);
}

template <typename T, class DecoderImpl>
T DecodeAs(DecoderImpl* decoder, size_t offset) {
  T value;
  Decode(decoder, &value, offset);
  return value;
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_CODING_TRAITS_H_
