// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(vardhan):  Currently, the logic for serializing a mojom type exists in
// two places: the C++ code generator template, and here.  However, most types
// are serialized the same way within Arrays or outside, with the exception of
// |bool|.  Consider defining serialization/deserialization traits for each
// serializable type and call those traits from here.  This should help us
// remove most of the ArraySerializer<> specializations here.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_ARRAY_SERIALIZATION_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_ARRAY_SERIALIZATION_H_

#include <string.h>  // For |memcpy()|.
#include <type_traits>
#include <vector>

#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/internal/bindings_internal.h"
#include "lib/fidl/cpp/bindings/internal/iterator_util.h"
#include "lib/fidl/cpp/bindings/internal/map_data_internal.h"
#include "lib/fidl/cpp/bindings/internal/map_serialization_forward.h"
#include "lib/fidl/cpp/bindings/internal/string_serialization.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"

namespace fidl {
namespace internal {

// The ArraySerializer template contains static methods for serializing |Array|s
// of various types.  These methods include:
//   * size_t GetSerializedSize(..)
//       Computes the size of the serialized version of the |Array|.
//   * void SerializeElements(..)
//       Takes an |Iterator| and a size and serializes it.
//   * void DeserializeElements(..)
//       Takes a pointer to an |Array_Data| and deserializes it into a given
//       |Array|.
//
// Note: The enable template parameter exists only to allow partial
// specializations to disable instantiation using logic based on E and F.
// By default, assuming that there are no other substitution failures, the
// specialization will instantiate and needs to take no action.  A partial
// specialization of the form
//
// template<E, F> struct ArraySerialzer<E, F>
//
// may be limited to values of E and F with particular properties by supplying
// an expression for enable which will cause substitution failure if the
// properties of E and F do not satisfy the expression.
template <typename E,
          typename F,
          bool is_union =
              IsUnionDataType<typename std::remove_pointer<F>::type>::value,
          typename enable = void>
struct ArraySerializer;

// Handles serialization and deserialization of arrays of pod types.
template <typename E, typename F>
struct ArraySerializer<
    E,
    F,
    false,
    typename std::enable_if<std::is_convertible<E, F>::value ||
                                (std::is_enum<E>::value &&
                                 std::is_same<F, int32_t>::value),
                            void>::type> {
  static_assert(sizeof(E) == sizeof(F), "Incorrect array serializer");
  static size_t GetSerializedSize(const Array<E>& input) {
    return sizeof(Array_Data<F>) + Align(input.size() * sizeof(E));
  }

  template <typename Iterator>
  static ValidationError SerializeElements(
      Iterator it,
      size_t num_elements,
      Buffer* buf,
      Array_Data<F>* output,
      const ArrayValidateParams* validate_params) {
    FXL_DCHECK(!validate_params->element_is_nullable)
        << "Primitive type should be non-nullable";
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Primitive type should not have array validate params";
    for (size_t i = 0; i < num_elements; ++i, ++it)
      output->at(i) = static_cast<F>(*it);

    return ValidationError::NONE;
  }

  // We can optimize serializing PODs by |memcpy|ing directly.
  // Note that this has precedence over its templated sibling defined above.
  static ValidationError SerializeElements(
      typename Array<E>::Iterator it,
      size_t num_elements,
      Buffer* buf,
      Array_Data<F>* output,
      const ArrayValidateParams* validate_params) {
    FXL_DCHECK(!validate_params->element_is_nullable)
        << "Primitive type should be non-nullable";
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Primitive type should not have array validate params";
    if (num_elements)
      memcpy(output->storage(), &(*it), num_elements * sizeof(E));

    return ValidationError::NONE;
  }

  static void DeserializeElements(Array_Data<F>* input, Array<E>* output) {
    std::vector<E> result(input->size());
    if (input->size())
      memcpy(&result[0], input->storage(), input->size() * sizeof(E));
    output->Swap(&result);
  }
};

// Serializes and deserializes arrays of bools.
template <>
struct ArraySerializer<bool, bool, false> {
  static size_t GetSerializedSize(const Array<bool>& input) {
    return sizeof(Array_Data<bool>) + Align((input.size() + 7) / 8);
  }

  template <typename Iterator>
  static ValidationError SerializeElements(
      Iterator it,
      size_t num_elements,
      Buffer* buf,
      Array_Data<bool>* output,
      const ArrayValidateParams* validate_params) {
    FXL_DCHECK(!validate_params->element_is_nullable)
        << "Primitive type should be non-nullable";
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Primitive type should not have array validate params";

    // TODO(darin): Can this be a memcpy somehow instead of a bit-by-bit copy?
    for (size_t i = 0; i < num_elements; ++i, ++it)
      output->at(i) = *it;

    return ValidationError::NONE;
  }

  static void DeserializeElements(Array_Data<bool>* input,
                                  Array<bool>* output) {
    auto result = Array<bool>::New(input->size());
    // TODO(darin): Can this be a memcpy somehow instead of a bit-by-bit copy?
    for (size_t i = 0; i < input->size(); ++i)
      result.at(i) = input->at(i);
    output->Swap(&result);
  }
};

// Serializes and deserializes arrays of handles.
template <typename H>
struct ArraySerializer<H,
                       WrappedHandle,
                       false,
                       typename std::enable_if<IsHandleType<H>::value>::type> {
  static size_t GetSerializedSize(const Array<H>& input) {
    return sizeof(Array_Data<WrappedHandle>) +
           Align(input.size() * sizeof(WrappedHandle));
  }

  template <typename Iterator>
  static ValidationError SerializeElements(
      Iterator it,
      size_t num_elements,
      Buffer* buf,
      Array_Data<WrappedHandle>* output,
      const ArrayValidateParams* validate_params) {
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Handle type should not have array validate params";

    for (size_t i = 0; i < num_elements; ++i, ++it) {
      // Transfer ownership of the handle.
      output->at(i) = WrappedHandle{it->release()};
      if (!validate_params->element_is_nullable &&
          output->at(i).value == MX_HANDLE_INVALID) {
        FIDL_INTERNAL_DLOG_SERIALIZATION_WARNING(
            ValidationError::UNEXPECTED_INVALID_HANDLE,
            MakeMessageWithArrayIndex(
                "invalid handle in array expecting valid handles", num_elements,
                i));
        return ValidationError::UNEXPECTED_INVALID_HANDLE;
      }
    }

    return ValidationError::NONE;
  }

  static void DeserializeElements(Array_Data<WrappedHandle>* input,
                                  Array<H>* output) {
    auto result = Array<H>::New(input->size());
    for (size_t i = 0; i < input->size(); ++i)
      result.at(i) = UnwrapHandle<H>(FetchAndReset(&input->at(i)));
    output->Swap(&result);
  }
};

// Serializes and deserializes arrays of interface requests.
template <typename I>
struct ArraySerializer<InterfaceRequest<I>, WrappedHandle, false> {
  static size_t GetSerializedSize(const Array<InterfaceRequest<I>>& input) {
    return sizeof(Array_Data<WrappedHandle>) +
           Align(input.size() * sizeof(WrappedHandle));
  }

  template <typename Iterator>
  static ValidationError SerializeElements(
      Iterator it,
      size_t num_elements,
      Buffer* buf,
      Array_Data<WrappedHandle>* output,
      const ArrayValidateParams* validate_params) {
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Handle type should not have array validate params";

    for (size_t i = 0; i < num_elements; ++i, ++it) {
      // Transfer ownership of the WrappedHandle.
      output->at(i) = WrappedHandle{it->PassChannel().release()};
      if (!validate_params->element_is_nullable &&
          output->at(i).value == MX_HANDLE_INVALID) {
        FIDL_INTERNAL_DLOG_SERIALIZATION_WARNING(
            ValidationError::UNEXPECTED_INVALID_HANDLE,
            MakeMessageWithArrayIndex(
                "invalid channel handle in array expecting valid handles",
                num_elements, i));
        return ValidationError::UNEXPECTED_INVALID_HANDLE;
      }
    }

    return ValidationError::NONE;
  }

  static void DeserializeElements(Array_Data<WrappedHandle>* input,
                                  Array<InterfaceRequest<I>>* output) {
    auto result = Array<InterfaceRequest<I>>::New(input->size());
    for (size_t i = 0; i < input->size(); ++i)
      result.at(i) = InterfaceRequest<I>(
          UnwrapHandle<mx::channel>(FetchAndReset(&input->at(i))));
    output->Swap(&result);
  }
};

// Serializes and deserializes arrays of interfaces (interface handles).
template <typename Interface>
struct ArraySerializer<InterfaceHandle<Interface>, Interface_Data, false> {
  static size_t GetSerializedSize(
      const Array<InterfaceHandle<Interface>>& input) {
    return sizeof(Array_Data<Interface_Data>) +
           Align(input.size() * sizeof(Interface_Data));
  }

  template <typename Iterator>
  static ValidationError SerializeElements(
      Iterator it,
      size_t num_elements,
      Buffer* buf,
      Array_Data<Interface_Data>* output,
      const ArrayValidateParams* validate_params) {
    FXL_DCHECK(!validate_params->element_validate_params)
        << "Interface type should not have array validate params";

    for (size_t i = 0; i < num_elements; ++i, ++it) {
      // Transfer ownership of the handle.
      internal::InterfaceHandleToData(std::move(*it), &output->at(i));
      if (!validate_params->element_is_nullable &&
          output->at(i).handle.value == MX_HANDLE_INVALID) {
        FIDL_INTERNAL_DLOG_SERIALIZATION_WARNING(
            ValidationError::UNEXPECTED_INVALID_HANDLE,
            MakeMessageWithArrayIndex(
                "invalid handle in array expecting valid handles", num_elements,
                i));
        return ValidationError::UNEXPECTED_INVALID_HANDLE;
      }
    }

    return ValidationError::NONE;
  }

  static void DeserializeElements(Array_Data<Interface_Data>* input,
                                  Array<InterfaceHandle<Interface>>* output) {
    auto result = Array<InterfaceHandle<Interface>>::New(input->size());
    for (size_t i = 0; i < input->size(); ++i)
      internal::InterfaceDataToHandle(&input->at(i), &result.at(i));
    output->Swap(&result);
  }
};

// This template must only apply to pointer mojo entity (structs, arrays,
// strings).  This is done by ensuring that WrapperTraits<S>::DataType is a
// pointer.
template <typename S>
struct ArraySerializer<
    S,
    typename std::enable_if<
        std::is_pointer<typename WrapperTraits<S>::DataType>::value,
        typename WrapperTraits<S>::DataType>::type,
    false> {
  using S_Data =
      typename std::remove_pointer<typename WrapperTraits<S>::DataType>::type;
  static size_t GetSerializedSize(const Array<S>& input) {
    size_t size = sizeof(Array_Data<S_Data*>) +
                  input.size() * sizeof(StructPointer<S_Data>);
    for (size_t i = 0; i < input.size(); ++i) {
      if (!input[i].is_null())
        size += GetSerializedSize_(*(UnwrapConstStructPtr<S>::value(input[i])));
    }
    return size;
  }

  template <typename Iterator>
  static ValidationError SerializeElements(
      Iterator it,
      size_t num_elements,
      Buffer* buf,
      Array_Data<S_Data*>* output,
      const ArrayValidateParams* validate_params) {
    for (size_t i = 0; i < num_elements; ++i, ++it) {
      S_Data* element;
      auto retval = SerializeCaller::Run(
          &(*it), buf, &element, validate_params->element_validate_params);
      if (retval != ValidationError::NONE)
        return retval;

      output->at(i) = element;
      if (!validate_params->element_is_nullable && !element) {
        FIDL_INTERNAL_DLOG_SERIALIZATION_WARNING(
            ValidationError::UNEXPECTED_NULL_POINTER,
            MakeMessageWithArrayIndex("null in array expecting valid pointers",
                                      num_elements, i));
        return ValidationError::UNEXPECTED_NULL_POINTER;
      }
    }

    return ValidationError::NONE;
  }

  static void DeserializeElements(Array_Data<S_Data*>* input,
                                  Array<S>* output) {
    auto result = Array<S>::New(input->size());
    for (size_t i = 0; i < input->size(); ++i) {
      DeserializeCaller::Run(input->at(i), &result[i]);
    }
    output->Swap(&result);
  }

 private:
  // SerializeCaller template is used by |ArraySerializer| to dispatch a
  // serialize call on a non-POD type.  This template is defined outside
  // |ArraySerializer| since you cannot specialize a struct within a class
  // definition.
  struct SerializeCaller {
    // This template needs to be suppressed if |T| is |String|, otherwise it
    // takes precedence over the |String|-overloaded Run() below.
    template <
        typename T,
        typename =
            typename std::enable_if<!std::is_same<T, String>::value, T>::type>
    static ValidationError Run(T* input,
                               Buffer* buf,
                               typename WrapperTraits<T>::DataType* output,
                               const ArrayValidateParams* validate_params) {
      FXL_DCHECK(!validate_params)
          << "Struct type should not have array validate params";
      return Serialize_(UnwrapStructPtr<T>::value(*input), buf, output);
    }

    static ValidationError Run(const String* input,
                               Buffer* buf,
                               String_Data** output,
                               const ArrayValidateParams* validate_params) {
      FXL_DCHECK(validate_params && !validate_params->element_validate_params &&
                 !validate_params->element_is_nullable &&
                 validate_params->expected_num_elements == 0)
          << "String type has unexpected array validate params";
      SerializeString_(*input, buf, output);
      return ValidationError::NONE;
    }

    template <typename T>
    static ValidationError Run(Array<T>* input,
                               Buffer* buf,
                               typename Array<T>::Data_** output,
                               const ArrayValidateParams* validate_params) {
      return SerializeArray_(input, buf, output, validate_params);
    }

    template <typename Key, typename Value>
    static ValidationError Run(Map<Key, Value>* input,
                               Buffer* buf,
                               typename Map<Key, Value>::Data_** output,
                               const ArrayValidateParams* validate_params) {
      return SerializeMap_(input, buf, output, validate_params);
    }
  };

  struct DeserializeCaller {
    template <typename T>
    static void Run(typename WrapperTraits<T>::DataType input, T* output) {
      Deserialize_(input, output);
    }

    // Since Deserialize_ takes in a |Struct*| (not |StructPtr|), we need to
    // initialize the |StructPtr| here before deserializing into its underlying
    // data.
    // TODO(vardhan):  Either all containers, or just Deserialize_(), should
    // support taking in an allocator.
    template <typename T>
    static void Run(typename WrapperTraits<StructPtr<T>>::DataType input,
                    StructPtr<T>* output) {
      if (input) {
        *output = T::New();
        Deserialize_(input, output->get());
      }
    }

    template <typename T>
    static void Run(typename WrapperTraits<InlinedStructPtr<T>>::DataType input,
                    InlinedStructPtr<T>* output) {
      if (input) {
        *output = T::New();
        Deserialize_(input, output->get());
      }
    }
  };
};

// Handles serialization and deserialization of arrays of unions.
template <typename U, typename U_Data>
struct ArraySerializer<U, U_Data, true> {
  static size_t GetSerializedSize(const Array<U>& input) {
    size_t size = sizeof(Array_Data<U_Data>);
    for (size_t i = 0; i < input.size(); ++i) {
      // GetSerializedSize_ will account for both the data in the union and the
      // space in the array used to hold the union.
      size += GetSerializedSize_(input[i]);
    }
    return size;
  }

  template <typename Iterator>
  static ValidationError SerializeElements(
      Iterator it,
      size_t num_elements,
      Buffer* buf,
      Array_Data<U_Data>* output,
      const ArrayValidateParams* validate_params) {
    for (size_t i = 0; i < num_elements; ++i, ++it) {
      U_Data* result = output->storage() + i;
      auto retval = SerializeUnion_(it->get(), buf, &result);
      if (retval != ValidationError::NONE)
        return retval;
      if (!validate_params->element_is_nullable && output->at(i).is_null()) {
        FIDL_INTERNAL_DLOG_SERIALIZATION_WARNING(

            ValidationError::UNEXPECTED_NULL_POINTER,
            MakeMessageWithArrayIndex("null in array expecting valid unions",
                                      num_elements, i));
        return ValidationError::UNEXPECTED_NULL_POINTER;
      }
    }

    return ValidationError::NONE;
  }

  static void DeserializeElements(Array_Data<U_Data>* input, Array<U>* output) {
    auto result = Array<U>::New(input->size());
    for (size_t i = 0; i < input->size(); ++i) {
      auto& elem = input->at(i);
      if (!elem.is_null()) {
        using UnwrapedUnionType = typename RemoveStructPtr<U>::type;
        result[i] = UnwrapedUnionType::New();
        Deserialize_(&elem, result[i].get());
      }
    }
    output->Swap(&result);
  }
};

}  // namespace internal

template <typename E>
inline size_t GetSerializedSize_(const Array<E>& input) {
  if (!input)
    return 0;
  using F = typename internal::WrapperTraits<E>::DataType;
  return internal::ArraySerializer<E, F>::GetSerializedSize(input);
}

// SerializeArray_ will return ValidationError::NONE on success and set
// |output| accordingly.  On failure, |input| will be partially serialized into
// |output| up until an error occurs (which is propagated up and returned by
// SerializeArray_), in which case |buf| is also partially consumed.
template <typename E, typename F>
inline internal::ValidationError SerializeArray_(
    Array<E>* input,
    internal::Buffer* buf,
    internal::Array_Data<F>** output,
    const internal::ArrayValidateParams* validate_params) {
  FXL_DCHECK(input);
  if (!*input) {
    // It is up to the caller to make sure the given |Array| is not null if it
    // is not nullable.
    *output = nullptr;
    return internal::ValidationError::NONE;
  }

  if (validate_params->expected_num_elements != 0 &&
      input->size() != validate_params->expected_num_elements) {
    FIDL_INTERNAL_DLOG_SERIALIZATION_WARNING(
        internal::ValidationError::UNEXPECTED_ARRAY_HEADER,
        internal::MakeMessageWithExpectedArraySize(
            "fixed-size array has wrong number of elements", input->size(),
            validate_params->expected_num_elements));
    return internal::ValidationError::UNEXPECTED_ARRAY_HEADER;
  }

  internal::Array_Data<F>* result =
      internal::Array_Data<F>::New(input->size(), buf);
  auto retval = internal::ArraySerializer<E, F>::SerializeElements(
      input->begin(), input->size(), buf, result, validate_params);
  if (retval != internal::ValidationError::NONE)
    return retval;

  *output = result;
  return internal::ValidationError::NONE;
}

template <typename E, typename F>
inline void Deserialize_(internal::Array_Data<F>* input, Array<E>* output) {
  if (input) {
    internal::ArraySerializer<E, F>::DeserializeElements(input, output);
  } else {
    output->reset();
  }
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_ARRAY_SERIALIZATION_H_
