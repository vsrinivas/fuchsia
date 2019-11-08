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
  static constexpr size_t inline_size_old = sizeof(T);
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
  static constexpr size_t inline_size_old = sizeof(bool);
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
  static constexpr size_t inline_size_old = sizeof(zx_handle_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(zx_handle_t);
  static void Encode(Encoder* encoder, zx::object_base* value, size_t offset) {
    encoder->EncodeHandle(value, offset);
  }
  static void Decode(Decoder* decoder, zx::object_base* value, size_t offset) {
    decoder->DecodeHandle(value, offset);
  }
};
#endif

template<typename T>
struct CodingTraits<std::unique_ptr<T>, typename std::enable_if<!IsFidlUnion<T>::value>::type> {
  static constexpr size_t inline_size_old = sizeof(uintptr_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(uintptr_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::unique_ptr<T>* value, size_t offset) {
    if (value->get()) {
      *encoder->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_PRESENT;
      size_t size;
      if (encoder->ShouldEncodeUnionAsXUnion()) {
        size = CodingTraits<T>::inline_size_v1_no_ee;
      } else {
        size = CodingTraits<T>::inline_size_old;
      }
      CodingTraits<T>::Encode(encoder, value->get(), encoder->Alloc(size));
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

template<typename T>
struct CodingTraits<std::unique_ptr<T>, typename std::enable_if<IsFidlUnion<T>::value>::type> {
  static constexpr size_t inline_size_old = sizeof(uintptr_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_xunion_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder_, std::unique_ptr<T>* value, size_t offset) {
    if (encoder_->ShouldEncodeUnionAsXUnion()) {
      EncodeAsXUnionBytes(encoder_, value, offset);
      return;
    }
    if (value->get()) {
      *encoder_->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_PRESENT;
      size_t size = CodingTraits<T>::inline_size_old;
      CodingTraits<T>::Encode(encoder_, value->get(), encoder_->Alloc(size));
    } else {
      *encoder_->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_ABSENT;
    }
  }
  template <class EncoderImpl>
  static void EncodeAsXUnionBytes(EncoderImpl* encoder_, std::unique_ptr<T>* value, size_t offset) {
    auto&& p_union = *value;
    if (p_union) {
      p_union->EncodeAsXUnionBytes(encoder_, offset);
    }
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder_, std::unique_ptr<T>* value, size_t offset) {
    uintptr_t ptr = *decoder_->template GetPtr<uintptr_t>(offset);
    if (!ptr)
      return value->reset();
    *value = std::make_unique<T>();
    CodingTraits<T>::Decode(decoder_, value->get(), decoder_->GetOffset(ptr));
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
  static constexpr size_t inline_size_old = sizeof(fidl_vector_t);
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
  static constexpr size_t inline_size_old = sizeof(fidl_vector_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, ::std::vector<T>* value, size_t offset) {
    size_t count = value->size();
    EncodeVectorPointer(encoder, count, offset);
    size_t stride;
    if (encoder->ShouldEncodeUnionAsXUnion()) {
      stride = CodingTraits<T>::inline_size_v1_no_ee;
    } else {
      stride = CodingTraits<T>::inline_size_old;
    }
    size_t base = encoder->Alloc(count * stride);
    for (size_t i = 0; i < count; ++i)
      CodingTraits<T>::Encode(encoder, &value->at(i), base + i * stride);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, ::std::vector<T>* value, size_t offset) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);
    value->resize(encoded->count);
    size_t stride = CodingTraits<T>::inline_size_old;
    size_t base = decoder->GetOffset(encoded->data);
    size_t count = encoded->count;
    for (size_t i = 0; i < count; ++i)
      CodingTraits<T>::Decode(decoder, &value->at(i), base + i * stride);
  }
};

template <typename T, size_t N>
struct CodingTraits<::std::array<T, N>> {
  static constexpr size_t inline_size_old = CodingTraits<T>::inline_size_old * N;
  static constexpr size_t inline_size_v1_no_ee = CodingTraits<T>::inline_size_v1_no_ee * N;
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::array<T, N>* value, size_t offset) {
    size_t stride;
    if (encoder->ShouldEncodeUnionAsXUnion()) {
      stride = CodingTraits<T>::inline_size_v1_no_ee;
    } else {
      stride = CodingTraits<T>::inline_size_old;
    }
    for (size_t i = 0; i < N; ++i)
      CodingTraits<T>::Encode(encoder, &value->at(i), offset + i * stride);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, std::array<T, N>* value, size_t offset) {
    size_t stride = CodingTraits<T>::inline_size_old;
    for (size_t i = 0; i < N; ++i)
      CodingTraits<T>::Decode(decoder, &value->at(i), offset + i * stride);
  }
};

template <typename T, size_t InlineSizeOld, size_t InlineSizeV1NoEE>
struct EncodableCodingTraits {
  static constexpr size_t inline_size_old = InlineSizeOld;
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
  if (encoder->ShouldEncodeUnionAsXUnion()) {
    return CodingTraits<T>::inline_size_v1_no_ee;
  }
  return CodingTraits<T>::inline_size_old;
}

template <typename T, class DecoderImpl = Decoder>
size_t DecodingInlineSize(DecoderImpl* decoder) {
  return CodingTraits<T>::inline_size_old;
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
