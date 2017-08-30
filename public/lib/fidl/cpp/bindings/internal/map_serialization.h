// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_MAP_SERIALIZATION_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_MAP_SERIALIZATION_H_

#include <type_traits>

#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/internal/array_serialization.h"
#include "lib/fidl/cpp/bindings/internal/bindings_internal.h"
#include "lib/fidl/cpp/bindings/internal/iterator_util.h"
#include "lib/fidl/cpp/bindings/internal/map_data_internal.h"
#include "lib/fidl/cpp/bindings/internal/map_internal.h"
#include "lib/fidl/cpp/bindings/internal/string_serialization.h"
#include "lib/fidl/cpp/bindings/internal/template_util.h"
#include "lib/fidl/cpp/bindings/map.h"

namespace fidl {
namespace internal {

template <typename MapType,
          typename DataType,
          bool value_is_move_only_type = IsMoveOnlyType<MapType>::value,
          bool is_union = IsUnionDataType<
              typename std::remove_pointer<DataType>::type>::value>
struct MapSerializer;

template <typename MapType, typename DataType>
struct MapSerializer<MapType, DataType, false, false> {
  static size_t GetBaseArraySize(size_t count) {
    return Align(count * sizeof(DataType));
  }
  static size_t GetItemSize(const MapType& item) { return 0; }
};

template <>
struct MapSerializer<bool, bool, false, false> {
  static size_t GetBaseArraySize(size_t count) {
    return Align((count + 7) / 8);
  }
  static size_t GetItemSize(bool item) { return 0; }
};

template <typename H>
struct MapSerializer<
    H,
    typename std::enable_if<IsHandleType<H>::value, WrappedHandle>::type,
    true, false> {
  static size_t GetBaseArraySize(size_t count) {
    return Align(count * sizeof(H));
  }
  static size_t GetItemSize(const H& item) { return 0; }
};

// This template must only apply to pointer mojo entity (structs and arrays).
// This is done by ensuring that WrapperTraits<S>::DataType is a pointer.
template <typename S>
struct MapSerializer<
    S,
    typename std::enable_if<
        std::is_pointer<typename WrapperTraits<S>::DataType>::value,
        typename WrapperTraits<S>::DataType>::type,
    true,
    false> {
  typedef
      typename std::remove_pointer<typename WrapperTraits<S>::DataType>::type
          S_Data;
  static size_t GetBaseArraySize(size_t count) {
    return count * sizeof(StructPointer<S_Data>);
  }
  static size_t GetItemSize(const S& item) {
    return item ? GetSerializedSize_(*UnwrapConstStructPtr<S>::value(item)) : 0;
  }
};

template <typename U, typename U_Data>
struct MapSerializer<U, U_Data, true, true> {
  static size_t GetBaseArraySize(size_t count) {
    // GetSerializedSize_ (called in GetItemSize()) will account for
    // sizeof(U_Data), so prevent double counting by having this count be 0.
    return 0;
  }
  static size_t GetItemSize(const U& item) { return GetSerializedSize_(item); }
};

template <>
struct MapSerializer<String, String_Data*, false, false> {
  static size_t GetBaseArraySize(size_t count) {
    return count * sizeof(StringPointer);
  }
  static size_t GetItemSize(const String& item) {
    return GetSerializedSize_(item);
  }
};

}  // namespace internal

// TODO(erg): This can't go away yet. We still need to calculate out the size
// of a struct header, and two arrays.
template <typename MapKey, typename MapValue>
inline size_t GetSerializedSize_(const Map<MapKey, MapValue>& input) {
  if (!input)
    return 0;
  typedef typename internal::WrapperTraits<MapKey>::DataType DataKey;
  typedef typename internal::WrapperTraits<MapValue>::DataType DataValue;

  size_t count = input.size();
  size_t struct_overhead = sizeof(fidl::internal::Map_Data<DataKey, DataValue>);
  size_t key_base_size =
      sizeof(internal::ArrayHeader) +
      internal::MapSerializer<MapKey, DataKey>::GetBaseArraySize(count);
  size_t value_base_size =
      sizeof(internal::ArrayHeader) +
      internal::MapSerializer<MapValue, DataValue>::GetBaseArraySize(count);

  size_t key_data_size = 0;
  size_t value_data_size = 0;
  for (auto it = input.cbegin(); it != input.cend(); ++it) {
    key_data_size +=
        internal::MapSerializer<MapKey, DataKey>::GetItemSize(it.GetKey());
    value_data_size +=
        internal::MapSerializer<MapValue, DataValue>::GetItemSize(
            it.GetValue());
  }

  return struct_overhead + key_base_size + key_data_size + value_base_size +
         value_data_size;
}

// SerializeMap_ will return ValidationError::NONE on success and set
// |output| accordingly.  On failure, |input| will be partially serialized into
// |output| up until an error occurs (which is propagated up and returned by
// SerializeMap_), in which case |buf| is also partially consumed.
//
// We don't need an ArrayValidateParams instance for key validation since
// we can deduce it from the Key type. (which can only be primitive types or
// non-nullable strings.)
template <typename MapKey,
          typename MapValue,
          typename DataKey,
          typename DataValue>
inline internal::ValidationError SerializeMap_(
    Map<MapKey, MapValue>* input,
    internal::Buffer* buf,
    internal::Map_Data<DataKey, DataValue>** output,
    const internal::ArrayValidateParams* value_validate_params) {
  if (input->is_null()) {
    // |input| could be a nullable map, in which case |output| is serialized as
    // null, which is valid.
    *output = nullptr;
    return internal::ValidationError::NONE;
  }

  internal::Map_Data<DataKey, DataValue>* result =
      internal::Map_Data<DataKey, DataValue>::New(buf);

  // We *must* serialize the keys before we allocate an Array_Data for the
  // values.
  internal::Array_Data<DataKey>* keys_data =
      internal::Array_Data<DataKey>::New(input->size(), buf);
  result->keys.ptr = keys_data;

  internal::MapKeyIterator<MapKey, MapValue> key_iter(input);
  const internal::ArrayValidateParams* key_validate_params =
      internal::MapKeyValidateParamsFactory<DataKey>::Get();

  auto keys_retval =
      internal::ArraySerializer<MapKey, DataKey>::SerializeElements(
          key_iter.begin(), input->size(), buf, result->keys.ptr,
          key_validate_params);
  if (keys_retval != internal::ValidationError::NONE)
    return keys_retval;

  // Now we try allocate an Array_Data for the values
  internal::Array_Data<DataValue>* values_data =
      internal::Array_Data<DataValue>::New(input->size(), buf);
  result->values.ptr = values_data;

  internal::MapValueIterator<MapKey, MapValue> value_iter(input);

  auto values_retval =
      internal::ArraySerializer<MapValue, DataValue>::SerializeElements(
          value_iter.begin(), input->size(), buf, result->values.ptr,
          value_validate_params);
  if (values_retval != internal::ValidationError::NONE)
    return values_retval;

  *output = result;
  return internal::ValidationError::NONE;
}

template <typename MapKey,
          typename MapValue,
          typename DataKey,
          typename DataValue>
inline void Deserialize_(internal::Map_Data<DataKey, DataValue>* input,
                         Map<MapKey, MapValue>* output) {
  if (input) {
    Array<MapKey> keys;
    Array<MapValue> values;

    Deserialize_(input->keys.ptr, &keys);
    Deserialize_(input->values.ptr, &values);

    *output = Map<MapKey, MapValue>(std::move(keys), std::move(values));
  } else {
    output->reset();
  }
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_MAP_SERIALIZATION_H_
