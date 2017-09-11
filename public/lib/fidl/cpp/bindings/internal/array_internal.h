// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_ARRAY_INTERNAL_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_ARRAY_INTERNAL_H_

#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "lib/fidl/cpp/bindings/internal/bindings_internal.h"
#include "lib/fidl/cpp/bindings/internal/bindings_serialization.h"
#include "lib/fidl/cpp/bindings/internal/bounds_checker.h"
#include "lib/fidl/cpp/bindings/internal/buffer.h"
#include "lib/fidl/cpp/bindings/internal/map_data_internal.h"
#include "lib/fidl/cpp/bindings/internal/validate_params.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fxl/logging.h"

namespace fidl {
template <typename T>
class Array;
class String;

namespace internal {

// std::numeric_limits<uint32_t>::max() is not a compile-time constant (until
// C++11).
const uint32_t kMaxUint32 = 0xFFFFFFFF;

// TODO(vardhan): Get rid of the following 2 functions.
std::string MakeMessageWithArrayIndex(const char* message,
                                      size_t size,
                                      size_t index);

std::string MakeMessageWithExpectedArraySize(const char* message,
                                             size_t size,
                                             size_t expected_size);

template <typename T>
struct ArrayDataTraits {
  typedef T StorageType;
  typedef T& Ref;
  typedef T const& ConstRef;

  static const uint32_t kMaxNumElements =
      (kMaxUint32 - sizeof(ArrayHeader)) / sizeof(StorageType);

  static uint32_t GetStorageSize(uint32_t num_elements) {
    FXL_DCHECK(num_elements <= kMaxNumElements);
    return sizeof(ArrayHeader) + sizeof(StorageType) * num_elements;
  }
  static Ref ToRef(StorageType* storage, size_t offset) {
    return storage[offset];
  }
  static ConstRef ToConstRef(const StorageType* storage, size_t offset) {
    return storage[offset];
  }
};

template <typename P, bool is_union>
struct ObjectStorageType;

template <typename P>
struct ObjectStorageType<P, true> {
  typedef P Type;
};

template <typename P>
struct ObjectStorageType<P, false> {
  typedef StructPointer<P> Type;
};

template <typename P>
struct ArrayDataTraits<P*> {
  typedef typename ObjectStorageType<P, IsUnionDataType<P>::value>::Type
      StorageType;
  typedef P*& Ref;
  typedef P* const& ConstRef;

  static const uint32_t kMaxNumElements =
      (kMaxUint32 - sizeof(ArrayHeader)) / sizeof(StorageType);

  static uint32_t GetStorageSize(uint32_t num_elements) {
    FXL_DCHECK(num_elements <= kMaxNumElements);
    return sizeof(ArrayHeader) + sizeof(StorageType) * num_elements;
  }
  static Ref ToRef(StorageType* storage, size_t offset) {
    return storage[offset].ptr;
  }
  static ConstRef ToConstRef(const StorageType* storage, size_t offset) {
    return storage[offset].ptr;
  }
};

template <typename T>
struct ArrayDataTraits<Array_Data<T>*> {
  typedef ArrayPointer<T> StorageType;
  typedef Array_Data<T>*& Ref;
  typedef Array_Data<T>* const& ConstRef;

  static const uint32_t kMaxNumElements =
      (kMaxUint32 - sizeof(ArrayHeader)) / sizeof(StorageType);

  static uint32_t GetStorageSize(uint32_t num_elements) {
    FXL_DCHECK(num_elements <= kMaxNumElements);
    return sizeof(ArrayHeader) + sizeof(StorageType) * num_elements;
  }
  static Ref ToRef(StorageType* storage, size_t offset) {
    return storage[offset].ptr;
  }
  static ConstRef ToConstRef(const StorageType* storage, size_t offset) {
    return storage[offset].ptr;
  }
};

// Specialization of Arrays for bools, optimized for space. It has the
// following differences from a generalized Array:
// * Each element takes up a single bit of memory.
// * Accessing a non-const single element uses a helper class |BitRef|, which
// emulates a reference to a bool.
template <>
struct ArrayDataTraits<bool> {
  // Helper class to emulate a reference to a bool, used for direct element
  // access.
  class BitRef {
   public:
    ~BitRef();
    BitRef& operator=(bool value);
    BitRef& operator=(const BitRef& value);
    operator bool() const;

   private:
    friend struct ArrayDataTraits<bool>;
    BitRef(uint8_t* storage, uint8_t mask);
    BitRef();
    uint8_t* storage_;
    uint8_t mask_;
  };

  // Because each element consumes only 1/8 byte.
  static const uint32_t kMaxNumElements = kMaxUint32;

  typedef uint8_t StorageType;
  typedef BitRef Ref;
  typedef bool ConstRef;

  static uint32_t GetStorageSize(uint32_t num_elements) {
    return sizeof(ArrayHeader) + ((num_elements + 7) / 8);
  }
  static BitRef ToRef(StorageType* storage, size_t offset) {
    return BitRef(&storage[offset / 8], 1 << (offset % 8));
  }
  static bool ToConstRef(const StorageType* storage, size_t offset) {
    return (storage[offset / 8] & (1 << (offset % 8))) != 0;
  }
};

// What follows is code to support the serialization of Array_Data<T>. There
// are two interesting cases: arrays of primitives/unions, arrays of non-union
// objects (structs, arrays and maps).
// Arrays of non-union objects are represented as arrays of pointers to objects.
// Arrays of primitives or unions are represented as arrays of the values
// themselves.

template <typename T, bool is_handle, bool is_union>
struct ArraySerializationHelper;

template <typename T>
struct ArraySerializationHelper<T, false, false> {
  typedef typename ArrayDataTraits<T>::StorageType ElementType;

  static void EncodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles) {}

  static void DecodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles) {}

  static ValidationError ValidateElements(
      const ArrayHeader* header,
      const ElementType* elements,
      BoundsChecker* bounds_checker,
      const ArrayValidateParams* validate_params,
      std::string* err) {
    FXL_DCHECK(!validate_params->element_is_nullable)
        << "Primitive type should be non-nullable";
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Primitive type should not have array validate params";
    return ValidationError::NONE;
  }
};

template <>
struct ArraySerializationHelper<WrappedHandle, true, false> {
  typedef ArrayDataTraits<WrappedHandle>::StorageType ElementType;

  static void EncodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles);

  static void DecodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles);

  static ValidationError ValidateElements(
      const ArrayHeader* header,
      const ElementType* elements,
      BoundsChecker* bounds_checker,
      const ArrayValidateParams* validate_params,
      std::string* err) {
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Handle type should not have array validate params";

    for (uint32_t i = 0; i < header->num_elements; ++i) {
      if (!validate_params->element_is_nullable &&
          elements[i].value == kEncodedInvalidHandleValue) {
        FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
            << "invalid handle in array expecting valid handles (array size="
            << header->num_elements << ", index = " << i << ")";
        return ValidationError::UNEXPECTED_INVALID_HANDLE;
      }
      if (!bounds_checker->ClaimHandle(elements[i])) {
        FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
        return ValidationError::ILLEGAL_HANDLE;
      }
    }
    return ValidationError::NONE;
  }
};

template <typename H>
struct ArraySerializationHelper<H, true, false> {
  typedef typename ArrayDataTraits<H>::StorageType ElementType;

  static void EncodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles) {
    ArraySerializationHelper<WrappedHandle, true,
                             false>::EncodePointersAndHandles(header, elements,
                                                              handles);
  }

  static void DecodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles) {
    ArraySerializationHelper<WrappedHandle, true,
                             false>::DecodePointersAndHandles(header, elements,
                                                              handles);
  }

  static ValidationError ValidateElements(
      const ArrayHeader* header,
      const ElementType* elements,
      BoundsChecker* bounds_checker,
      const ArrayValidateParams* validate_params,
      std::string* err) {
    return ArraySerializationHelper<WrappedHandle, true,
                                    false>::ValidateElements(header, elements,
                                                             bounds_checker,
                                                             validate_params,
                                                             err);
  }
};

template <typename P>
struct ArraySerializationHelper<P*, false, false> {
  typedef typename ArrayDataTraits<P*>::StorageType ElementType;

  static void EncodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles) {
    for (uint32_t i = 0; i < header->num_elements; ++i)
      Encode(&elements[i], handles);
  }

  static void DecodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles) {
    for (uint32_t i = 0; i < header->num_elements; ++i)
      Decode(&elements[i], handles);
  }

  static ValidationError ValidateElements(
      const ArrayHeader* header,
      const ElementType* elements,
      BoundsChecker* bounds_checker,
      const ArrayValidateParams* validate_params,
      std::string* err) {
    for (uint32_t i = 0; i < header->num_elements; ++i) {
      if (!validate_params->element_is_nullable && !elements[i].offset) {
        FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
            << "null in array expecting valid pointers (size="
            << header->num_elements << ", index = " << i << ")";
        return ValidationError::UNEXPECTED_NULL_POINTER;
      }

      if (!ValidateEncodedPointer(&elements[i].offset)) {
        FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
        return ValidationError::ILLEGAL_POINTER;
      }

      auto retval = ValidateCaller<P>::Run(
          DecodePointerRaw(&elements[i].offset), bounds_checker,
          validate_params->element_validate_params, err);
      if (retval != ValidationError::NONE)
        return retval;
    }
    return ValidationError::NONE;
  }

 private:
  template <typename T>
  struct ValidateCaller {
    static ValidationError Run(const void* data,
                               BoundsChecker* bounds_checker,
                               const ArrayValidateParams* validate_params,
                               std::string* err) {
      FXL_DCHECK(!validate_params)
          << "Struct type should not have array validate params";

      return T::Validate(data, bounds_checker, err);
    }
  };

  template <typename Key, typename Value>
  struct ValidateCaller<Map_Data<Key, Value>> {
    static ValidationError Run(const void* data,
                               BoundsChecker* bounds_checker,
                               const ArrayValidateParams* validate_params,
                               std::string* err) {
      return Map_Data<Key, Value>::Validate(data, bounds_checker,
                                            validate_params, err);
    }
  };

  template <typename T>
  struct ValidateCaller<Array_Data<T>> {
    static ValidationError Run(const void* data,
                               BoundsChecker* bounds_checker,
                               const ArrayValidateParams* validate_params,
                               std::string* err) {
      return Array_Data<T>::Validate(data, bounds_checker, validate_params,
                                     err);
    }
  };
};

// Array Serialization Helper for unions.
template <typename P>
struct ArraySerializationHelper<P, false, true> {
  typedef P ElementType;

  static void EncodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles) {
    for (uint32_t i = 0; i < header->num_elements; ++i)
      elements[i].EncodePointersAndHandles(handles);
  }

  static void DecodePointersAndHandles(const ArrayHeader* header,
                                       ElementType* elements,
                                       std::vector<mx_handle_t>* handles) {
    for (uint32_t i = 0; i < header->num_elements; ++i)
      elements[i].DecodePointersAndHandles(handles);
  }

  static ValidationError ValidateElements(
      const ArrayHeader* header,
      const ElementType* elements,
      BoundsChecker* bounds_checker,
      const ArrayValidateParams* validate_params,
      std::string* err) {
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Union type should not have array validate params";
    for (uint32_t i = 0; i < header->num_elements; ++i) {
      if (!validate_params->element_is_nullable && elements[i].is_null()) {
        FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
            << "null union in array expecting non-null unions (size="
            << header->num_elements << ", index = " << i << ")";
        return ValidationError::UNEXPECTED_NULL_UNION;
      }

      auto retval = ElementType::Validate(
          static_cast<const void*>(&elements[i]), bounds_checker, true, err);
      if (retval != ValidationError::NONE)
        return retval;
    }
    return ValidationError::NONE;
  }
};

template <typename T>
class Array_Data {
 public:
  typedef ArrayDataTraits<T> Traits;
  typedef typename Traits::StorageType StorageType;
  typedef typename Traits::Ref Ref;
  typedef typename Traits::ConstRef ConstRef;
  typedef ArraySerializationHelper<
      T,
      IsWrappedHandle<T>::value,
      IsUnionDataType<typename std::remove_pointer<T>::type>::value>
      Helper;

  // Returns null if |num_elements| or the corresponding storage size cannot be
  // stored in uint32_t.
  static Array_Data<T>* New(size_t num_elements, Buffer* buf) {
    if (num_elements > Traits::kMaxNumElements)
      return nullptr;

    uint32_t num_bytes =
        Traits::GetStorageSize(static_cast<uint32_t>(num_elements));
    return new (buf->Allocate(num_bytes))
        Array_Data<T>(num_bytes, static_cast<uint32_t>(num_elements));
  }

  static ValidationError Validate(const void* data,
                                  BoundsChecker* bounds_checker,
                                  const ArrayValidateParams* validate_params,
                                  std::string* err) {
    if (!data)
      return ValidationError::NONE;
    if (!IsAligned(data)) {
      FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
      return ValidationError::MISALIGNED_OBJECT;
    }
    if (!bounds_checker->IsValidRange(data, sizeof(ArrayHeader))) {
      FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
      return ValidationError::ILLEGAL_MEMORY_RANGE;
    }

    const ArrayHeader* header = static_cast<const ArrayHeader*>(data);
    if (header->num_elements > Traits::kMaxNumElements ||
        header->num_bytes < Traits::GetStorageSize(header->num_elements)) {
      FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
      return ValidationError::UNEXPECTED_ARRAY_HEADER;
    }

    if (validate_params->expected_num_elements != 0 &&
        header->num_elements != validate_params->expected_num_elements) {
      FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err)
          << "fixed-size array has wrong number of elements (size="
          << header->num_elements
          << ", expected size=" << validate_params->expected_num_elements
          << ")";
      return ValidationError::UNEXPECTED_ARRAY_HEADER;
    }

    if (!bounds_checker->ClaimMemory(data, header->num_bytes)) {
      FIDL_INTERNAL_DEBUG_SET_ERROR_MSG(err) << "";
      return ValidationError::ILLEGAL_MEMORY_RANGE;
    }

    const Array_Data<T>* object = static_cast<const Array_Data<T>*>(data);
    return Helper::ValidateElements(&object->header_, object->storage(),
                                    bounds_checker, validate_params, err);
  }

  size_t size() const { return header_.num_elements; }

  Ref at(size_t offset) {
    FXL_DCHECK(offset < static_cast<size_t>(header_.num_elements));
    return Traits::ToRef(storage(), offset);
  }

  ConstRef at(size_t offset) const {
    FXL_DCHECK(offset < static_cast<size_t>(header_.num_elements));
    return Traits::ToConstRef(storage(), offset);
  }

  StorageType* storage() {
    return reinterpret_cast<StorageType*>(reinterpret_cast<char*>(this) +
                                          sizeof(*this));
  }

  const StorageType* storage() const {
    return reinterpret_cast<const StorageType*>(
        reinterpret_cast<const char*>(this) + sizeof(*this));
  }

  void EncodePointersAndHandles(std::vector<mx_handle_t>* handles) {
    Helper::EncodePointersAndHandles(&header_, storage(), handles);
  }

  void DecodePointersAndHandles(std::vector<mx_handle_t>* handles) {
    Helper::DecodePointersAndHandles(&header_, storage(), handles);
  }

 private:
  Array_Data(uint32_t num_bytes, uint32_t num_elements) {
    header_.num_bytes = num_bytes;
    header_.num_elements = num_elements;
  }
  ~Array_Data() = delete;

  internal::ArrayHeader header_;

  // Elements of type internal::ArrayDataTraits<T>::StorageType follow.
};
static_assert(sizeof(Array_Data<char>) == 8, "Bad sizeof(Array_Data)");

// UTF-8 encoded
typedef Array_Data<char> String_Data;

template <typename T, bool kIsMoveOnlyType>
struct ArrayTraits {};

template <typename T>
struct ArrayTraits<T, false> {
  typedef typename std::vector<T>::const_reference ForwardType;
  static inline void PushBack(std::vector<T>* vec, ForwardType value) {
    vec->push_back(value);
  }
  static inline void Clone(const std::vector<T>& src_vec,
                           std::vector<T>* dest_vec) {
    dest_vec->assign(src_vec.begin(), src_vec.end());
  }
};

template <typename T>
struct ArrayTraits<T, true> {
  typedef T ForwardType;
  static inline void PushBack(std::vector<T>* vec, T& value) {
    vec->push_back(std::move(value));
  }
  static inline void Clone(const std::vector<T>& src_vec,
                           std::vector<T>* dest_vec) {
    dest_vec->resize(src_vec.size());
    for (size_t i = 0; i < src_vec.size(); ++i)
      dest_vec->at(i) = src_vec.at(i).Clone();
  }
};

template <>
struct WrapperTraits<String, false> {
  typedef String_Data* DataType;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_ARRAY_INTERNAL_H_
