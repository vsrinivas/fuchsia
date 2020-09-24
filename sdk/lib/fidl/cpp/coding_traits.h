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
#include "lib/fidl/cpp/types.h"
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

namespace internal {

template <bool Value>
class UseStdCopy {};

template <typename T, typename EncoderImpl>
void EncodeVectorBody(UseStdCopy<true>, EncoderImpl* encoder,
                      typename std::vector<T>::iterator in_begin,
                      typename std::vector<T>::iterator in_end, size_t out_offset) {
  static_assert(CodingTraits<T>::inline_size_v1_no_ee == sizeof(T),
                "stride doesn't match object size");
  std::copy(in_begin, in_end, encoder->template GetPtr<T>(out_offset));
}

template <typename T, typename EncoderImpl>
void EncodeVectorBody(UseStdCopy<false>, EncoderImpl* encoder,
                      typename std::vector<T>::iterator in_begin,
                      typename std::vector<T>::iterator in_end, size_t out_offset) {
  constexpr size_t stride = CodingTraits<T>::inline_size_v1_no_ee;
  for (typename std::vector<T>::iterator in_it = in_begin; in_it != in_end;
       in_it++, out_offset += stride) {
    CodingTraits<T>::Encode(encoder, &*in_it, out_offset);
  }
}

template <typename T, typename DecoderImpl>
void DecodeVectorBody(UseStdCopy<true>, DecoderImpl* decoder, size_t in_begin_offset,
                      size_t in_end_offset, std::vector<T>* out, size_t count) {
  static_assert(CodingTraits<T>::inline_size_v1_no_ee == sizeof(T),
                "stride doesn't match object size");
  *out = std::vector<T>(decoder->template GetPtr<T>(in_begin_offset),
                        decoder->template GetPtr<T>(in_end_offset));
}

template <typename T, typename DecoderImpl>
void DecodeVectorBody(UseStdCopy<false>, DecoderImpl* decoder, size_t in_begin_offset,
                      size_t in_end_offset, std::vector<T>* out, size_t count) {
  out->resize(count);
  constexpr size_t stride = CodingTraits<T>::inline_size_v1_no_ee;
  size_t in_offset = in_begin_offset;
  typename std::vector<T>::iterator out_it = out->begin();
  for (; in_offset < in_end_offset; in_offset += stride, out_it++) {
    CodingTraits<T>::Decode(decoder, &*out_it, in_offset);
  }
}

}  // namespace internal

template <typename T>
struct CodingTraits<::std::vector<T>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, ::std::vector<T>* value, size_t offset) {
    size_t count = value->size();
    EncodeVectorPointer(encoder, count, offset);
    constexpr size_t stride = CodingTraits<T>::inline_size_v1_no_ee;
    size_t base = encoder->Alloc(count * stride);
    internal::EncodeVectorBody<T>(internal::UseStdCopy<IsMemcpyCompatible<T>::value>(), encoder,
                                  value->begin(), value->end(), base);
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, ::std::vector<T>* value, size_t offset) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);
    constexpr size_t stride = CodingTraits<T>::inline_size_v1_no_ee;
    size_t base = decoder->GetOffset(encoded->data);
    size_t count = encoded->count;
    internal::DecodeVectorBody<T>(internal::UseStdCopy<IsMemcpyCompatible<T>::value>(), decoder,
                                  base, base + stride * count, value, count);
  }
};

template <typename T, size_t N>
struct CodingTraits<::std::array<T, N>> {
  static constexpr size_t inline_size_v1_no_ee = CodingTraits<T>::inline_size_v1_no_ee * N;
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, std::array<T, N>* value, size_t offset) {
    size_t stride;
    stride = CodingTraits<T>::inline_size_v1_no_ee;
    if (IsMemcpyCompatible<T>::value) {
      memcpy(encoder->template GetPtr<void>(offset), &value[0], N * stride);
      return;
    }
    for (size_t i = 0; i < N; ++i) {
      CodingTraits<T>::Encode(encoder, &value->at(i), offset + i * stride);
    }
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, std::array<T, N>* value, size_t offset) {
    size_t stride = CodingTraits<T>::inline_size_v1_no_ee;
    if (IsMemcpyCompatible<T>::value) {
      memcpy(&value[0], decoder->template GetPtr<void>(offset), N * stride);
      return;
    }
    for (size_t i = 0; i < N; ++i) {
      CodingTraits<T>::Decode(decoder, &value->at(i), offset + i * stride);
    }
  }
};

template <>
struct CodingTraits<UnknownBytes> {
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, UnknownBytes* value, size_t offset) {
    std::copy(value->bytes.begin(), value->bytes.end(), encoder->template GetPtr<uint8_t>(offset));
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, UnknownBytes* value, size_t offset) {
    memcpy(value->bytes.data(), decoder->template GetPtr<void>(offset), value->bytes.size());
  }
};

#ifdef __Fuchsia__
template <>
struct CodingTraits<UnknownData> {
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, UnknownData* value, size_t offset) {
    std::copy(value->bytes.begin(), value->bytes.end(), encoder->template GetPtr<uint8_t>(offset));
    for (auto& handle : value->handles) {
      encoder->EncodeUnknownHandle(&handle);
    }
  }
  template <class DecoderImpl>
  static void Decode(DecoderImpl* decoder, UnknownData* value, size_t offset) {
    memcpy(value->bytes.data(), decoder->template GetPtr<void>(offset), value->bytes.size());
    for (auto& h : value->handles) {
      h = decoder->ClaimHandle();
    }
  }
};
#endif

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
