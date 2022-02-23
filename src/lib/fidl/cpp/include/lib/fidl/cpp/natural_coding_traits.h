// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_CODING_TRAITS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_CODING_TRAITS_H_

#include <lib/fidl/cpp/natural_decoder.h>
#include <lib/fidl/cpp/natural_encoder.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/stdcompat/optional.h>
#include <lib/zx/channel.h>

#include <string>

namespace fidl::internal {

// Used for handle rights and type checking during write and decode.
struct NaturalHandleInformation {
  zx_obj_type_t object_type;
  zx_rights_t rights;
};

template <typename T, class Enable = void>
struct NaturalCodingTraits;

template <typename T>
size_t NaturalEncodingInlineSize(NaturalEncoder* encoder) {
  switch (encoder->wire_format()) {
    case ::fidl::internal::WireFormatVersion::kV1:
      return NaturalCodingTraits<T>::inline_size_v1_no_ee;
    case ::fidl::internal::WireFormatVersion::kV2:
      return NaturalCodingTraits<T>::inline_size_v2;
  }
  __builtin_unreachable();
}

template <typename T>
size_t NaturalDecodingInlineSize(NaturalDecoder* decoder) {
  return NaturalCodingTraits<T>::inline_size_v2;
}

template <typename T>
struct NaturalCodingTraits<T, typename std::enable_if<NaturalIsPrimitive<T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(T);
  static constexpr size_t inline_size_v2 = sizeof(T);
  static void Encode(NaturalEncoder* encoder, T* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(maybe_handle_info == cpp17::nullopt);
    *encoder->template GetPtr<T>(offset) = *value;
  }
  static void Decode(NaturalDecoder* decoder, T* value, size_t offset) {
    *value = *decoder->template GetPtr<T>(offset);
  }
};

template <>
struct NaturalCodingTraits<bool> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(bool);
  static constexpr size_t inline_size_v2 = sizeof(bool);
  static void Encode(NaturalEncoder* encoder, bool* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    *encoder->template GetPtr<bool>(offset) = *value;
  }
  static void Encode(NaturalEncoder* encoder, std::vector<bool>::iterator value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    *encoder->template GetPtr<bool>(offset) = *value;
  }
  static void Decode(NaturalDecoder* decoder, bool* value, size_t offset) {
    *value = *decoder->template GetPtr<bool>(offset);
  }
  static void Decode(NaturalDecoder* decoder, std::vector<bool>::iterator value, size_t offset) {
    *value = *decoder->template GetPtr<bool>(offset);
  }
};

template <bool Value>
class NaturalUseStdCopy {};

template <typename T>
void NaturalEncodeVectorBody(NaturalUseStdCopy<true>, NaturalEncoder* encoder,
                             typename std::vector<T>::iterator in_begin,
                             typename std::vector<T>::iterator in_end, size_t out_offset,
                             cpp17::optional<NaturalHandleInformation> maybe_handle_info) {
  static_assert(NaturalCodingTraits<T>::inline_size_v1_no_ee == sizeof(T),
                "stride doesn't match object size");
  std::copy(in_begin, in_end, encoder->template GetPtr<T>(out_offset));
}

template <typename T>
void NaturalEncodeVectorBody(NaturalUseStdCopy<false>, NaturalEncoder* encoder,
                             typename std::vector<T>::iterator in_begin,
                             typename std::vector<T>::iterator in_end, size_t out_offset,
                             cpp17::optional<NaturalHandleInformation> maybe_handle_info) {
  size_t stride = NaturalEncodingInlineSize<T>(encoder);
  for (typename std::vector<T>::iterator in_it = in_begin; in_it != in_end;
       in_it++, out_offset += stride) {
    NaturalCodingTraits<T>::Encode(encoder, &*in_it, out_offset, maybe_handle_info);
  }
}

template <typename T>
void NaturalDecodeVectorBody(NaturalUseStdCopy<true>, NaturalDecoder* decoder,
                             size_t in_begin_offset, size_t in_end_offset, std::vector<T>* out,
                             size_t count) {
  static_assert(NaturalCodingTraits<T>::inline_size_v1_no_ee == sizeof(T),
                "stride doesn't match object size");
  *out = std::vector<T>(decoder->template GetPtr<T>(in_begin_offset),
                        decoder->template GetPtr<T>(in_end_offset));
}

template <typename T>
void NaturalDecodeVectorBody(NaturalUseStdCopy<false>, NaturalDecoder* decoder,
                             size_t in_begin_offset, size_t in_end_offset, std::vector<T>* out,
                             size_t count) {
  out->resize(count);
  size_t stride = NaturalDecodingInlineSize<T>(decoder);
  size_t in_offset = in_begin_offset;
  typename std::vector<T>::iterator out_it = out->begin();
  for (; in_offset < in_end_offset; in_offset += stride, out_it++) {
    NaturalCodingTraits<T>::Decode(decoder, &*out_it, in_offset);
  }
}

template <typename T>
struct NaturalCodingTraits<::std::vector<T>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_vector_t);
  static void Encode(NaturalEncoder* encoder, ::std::vector<T>* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    size_t count = value->size();
    fidl_vector_t* vector = encoder->template GetPtr<fidl_vector_t>(offset);
    vector->count = count;
    vector->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    size_t stride = NaturalEncodingInlineSize<T>(encoder);
    size_t base = encoder->Alloc(count * stride);
    internal::NaturalEncodeVectorBody<T>(
        internal::NaturalUseStdCopy<NaturalIsMemcpyCompatible<T>::value>(), encoder, value->begin(),
        value->end(), base, maybe_handle_info);
  }
  static void Decode(NaturalDecoder* decoder, ::std::vector<T>* value, size_t offset) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);
    size_t stride = NaturalDecodingInlineSize<T>(decoder);
    size_t base = decoder->GetOffset(encoded->data);
    size_t count = encoded->count;
    internal::NaturalDecodeVectorBody<T>(
        internal::NaturalUseStdCopy<NaturalIsMemcpyCompatible<T>::value>(), decoder, base,
        base + stride * count, value, count);
  }
};

template <typename T, size_t N>
struct NaturalCodingTraits<::std::array<T, N>> {
  static constexpr size_t inline_size_v1_no_ee = NaturalCodingTraits<T>::inline_size_v1_no_ee * N;
  static constexpr size_t inline_size_v2 = NaturalCodingTraits<T>::inline_size_v2 * N;
  static void Encode(NaturalEncoder* encoder, std::array<T, N>* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    size_t stride;
    stride = NaturalEncodingInlineSize<T>(encoder);
    if (NaturalIsMemcpyCompatible<T>::value) {
      memcpy(encoder->template GetPtr<void>(offset), &value[0], N * stride);
      return;
    }
    for (size_t i = 0; i < N; ++i) {
      NaturalCodingTraits<T>::Encode(encoder, &value->at(i), offset + i * stride,
                                     maybe_handle_info);
    }
  }
  static void Decode(NaturalDecoder* decoder, std::array<T, N>* value, size_t offset) {
    size_t stride = NaturalDecodingInlineSize<T>(decoder);
    if (NaturalIsMemcpyCompatible<T>::value) {
      memcpy(&value[0], decoder->template GetPtr<void>(offset), N * stride);
      return;
    }
    for (size_t i = 0; i < N; ++i) {
      NaturalCodingTraits<T>::Decode(decoder, &value->at(i), offset + i * stride);
    }
  }
};

#ifdef __Fuchsia__
template <typename T>
struct NaturalCodingTraits<
    T, typename std::enable_if<std::is_base_of<zx::object_base, T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(zx_handle_t);
  static constexpr size_t inline_size_v2 = sizeof(zx_handle_t);
  static void Encode(NaturalEncoder* encoder, zx::object_base* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_ASSERT(maybe_handle_info);
    encoder->EncodeHandle(value->release(),
                          {
                              .obj_type = maybe_handle_info->object_type,
                              .rights = maybe_handle_info->rights,
                          },
                          offset);
  }
  static void Decode(NaturalDecoder* decoder, zx::object_base* value, size_t offset) {
    decoder->DecodeHandle(value, offset);
  }
};
#endif  // __Fuchsia__

template <typename T>
struct NaturalCodingTraits<cpp17::optional<std::vector<T>>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_vector_t);

  static void Encode(NaturalEncoder* encoder, cpp17::optional<std::vector<T>>* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    if (value->has_value()) {
      fidl::internal::NaturalCodingTraits<std::vector<T>>::Encode(encoder, &value->value(), offset,
                                                                  maybe_handle_info);
      return;
    }
    fidl_vector_t* vec = encoder->template GetPtr<fidl_vector_t>(offset);
    vec->count = 0;
    vec->data = reinterpret_cast<char*>(FIDL_ALLOC_ABSENT);
  }
  static void Decode(NaturalDecoder* decoder, cpp17::optional<std::vector<T>>* value,
                     size_t offset) {
    fidl_vector_t* vec = decoder->template GetPtr<fidl_vector_t>(offset);
    if (vec->data == nullptr) {
      ZX_ASSERT(vec->count == 0);
      value->reset();
      return;
    }
    std::vector<T> unwrapped;
    fidl::internal::NaturalCodingTraits<std::vector<T>>::Decode(decoder, &unwrapped, offset);
    value->emplace(std::move(unwrapped));
  }
};

template <typename T>
struct NaturalCodingTraits<std::unique_ptr<T>, typename std::enable_if<!IsUnion<T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(uintptr_t);
  static constexpr size_t inline_size_v2 = sizeof(uintptr_t);
  static void Encode(NaturalEncoder* encoder, std::unique_ptr<T>* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    if (value->get()) {
      *encoder->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_PRESENT;

      size_t alloc_size = NaturalEncodingInlineSize<T>(encoder);
      NaturalCodingTraits<T>::Encode(encoder, value->get(), encoder->Alloc(alloc_size),
                                     maybe_handle_info);
    } else {
      *encoder->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_ABSENT;
    }
  }
  static void Decode(NaturalDecoder* decoder, std::unique_ptr<T>* value, size_t offset) {
    uintptr_t ptr = *decoder->template GetPtr<uintptr_t>(offset);
    if (!ptr)
      return value->reset();
    *value = std::make_unique<T>();
    NaturalCodingTraits<T>::Decode(decoder, value->get(), decoder->GetOffset(ptr));
  }
};

template <typename T>
struct NaturalCodingTraits<std::unique_ptr<T>, typename std::enable_if<IsUnion<T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_xunion_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_xunion_v2_t);

  static void Encode(NaturalEncoder* encoder, std::unique_ptr<T>* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    if (*value) {
      NaturalCodingTraits<T>::Encode(encoder, value->get(), offset, maybe_handle_info);
      return;
    }

    // Buffer is zero-initialized.
  }

  static void Decode(NaturalDecoder* decoder, std::unique_ptr<T>* value, size_t offset) {
    fidl_xunion_v2_t* u = decoder->template GetPtr<fidl_xunion_v2_t>(offset);
    if (FidlIsZeroEnvelope(&u->envelope)) {
      *value = nullptr;
      return;
    }
    *value = std::make_unique<T>();
    NaturalCodingTraits<T>::Decode(decoder, value->get(), offset);
  }
};

template <>
struct NaturalCodingTraits<::std::string> final {
  static constexpr size_t inline_size_old = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_string_t);
  static void Encode(NaturalEncoder* encoder, std::string* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(!maybe_handle_info);
    const size_t size = value->size();
    fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
    string->size = size;
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    size_t base = encoder->Alloc(size);
    char* payload = encoder->template GetPtr<char>(base);
    memcpy(payload, value->data(), size);
  }
  static void Decode(NaturalDecoder* decoder, std::string* value, size_t offset) {
    fidl_string_t* string = decoder->template GetPtr<fidl_string_t>(offset);
    ZX_ASSERT(string->data != nullptr);
    *value = std::string(string->data, string->size);
  }
};

template <>
struct NaturalCodingTraits<cpp17::optional<std::string>> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_string_t);

  static void Encode(NaturalEncoder* encoder, cpp17::optional<std::string>* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(!maybe_handle_info.has_value());
    if (value->has_value()) {
      fidl::internal::NaturalCodingTraits<std::string>::Encode(encoder, &value->value(), offset);
      return;
    }
    fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
    string->size = 0;
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_ABSENT);
  }
  static void Decode(NaturalDecoder* decoder, cpp17::optional<std::string>* value, size_t offset) {
    fidl_string_t* string = decoder->template GetPtr<fidl_string_t>(offset);
    if (string->data == nullptr) {
      ZX_ASSERT(string->size == 0);
      value->reset();
      return;
    }
    std::string unwrapped;
    fidl::internal::NaturalCodingTraits<std::string>::Decode(decoder, &unwrapped, offset);
    value->emplace(unwrapped);
  }
};

template <typename T>
struct NaturalCodingTraits<ClientEnd<T>> {
  static void Encode(NaturalEncoder* encoder, ClientEnd<T>* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(maybe_handle_info);
    encoder->EncodeHandle(value->TakeChannel().release(),
                          {
                              .obj_type = maybe_handle_info->object_type,
                              .rights = maybe_handle_info->rights,
                          },
                          offset);
  }

  static void Decode(NaturalDecoder* decoder, ClientEnd<T>* value, size_t offset) {
    zx::channel channel;
    decoder->DecodeHandle(&channel, offset);
    *value = ClientEnd<T>(std::move(channel));
  }
};

template <typename T>
struct NaturalCodingTraits<ServerEnd<T>> {
  static void Encode(NaturalEncoder* encoder, ServerEnd<T>* value, size_t offset,
                     cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_DEBUG_ASSERT(maybe_handle_info);
    encoder->EncodeHandle(value->TakeChannel().release(),
                          {
                              .obj_type = maybe_handle_info->object_type,
                              .rights = maybe_handle_info->rights,
                          },
                          offset);
  }

  static void Decode(NaturalDecoder* decoder, ServerEnd<T>* value, size_t offset) {
    zx::channel channel;
    decoder->DecodeHandle(&channel, offset);
    *value = ServerEnd<T>(std::move(channel));
  }
};

template <typename T>
void NaturalEncode(NaturalEncoder* encoder, T* value, size_t offset,
                   cpp17::optional<NaturalHandleInformation> maybe_handle_info = cpp17::nullopt) {
  NaturalCodingTraits<T>::Encode(encoder, value, offset, maybe_handle_info);
}

template <typename T>
void NaturalDecode(NaturalDecoder* decoder, T* value, size_t offset) {
  NaturalCodingTraits<T>::Decode(decoder, value, offset);
}

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_CODING_TRAITS_H_
