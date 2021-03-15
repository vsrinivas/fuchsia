// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolothertypes banjo file

#pragma once

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_interface_protocol_value, InterfaceValue,
        void (C::*)(const other_types_protocol_t* intf, other_types_protocol_t* out_intf));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_interface_protocol_reference, InterfaceReference,
        void (C::*)(const other_types_protocol_t* intf, other_types_protocol_t** out_intf));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_interface_protocol_async, InterfaceAsync,
        void (C::*)(const other_types_protocol_t* intf, interface_async_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_interface_protocol_async_refernce, InterfaceAsyncRefernce,
        void (C::*)(const other_types_protocol_t* intf, interface_async_refernce_callback callback, void* cookie));


template <typename D>
constexpr void CheckInterfaceProtocolSubclass() {
    static_assert(internal::has_interface_protocol_value<D>::value,
        "InterfaceProtocol subclasses must implement "
        "void InterfaceValue(const other_types_protocol_t* intf, other_types_protocol_t* out_intf);");

    static_assert(internal::has_interface_protocol_reference<D>::value,
        "InterfaceProtocol subclasses must implement "
        "void InterfaceReference(const other_types_protocol_t* intf, other_types_protocol_t** out_intf);");

    static_assert(internal::has_interface_protocol_async<D>::value,
        "InterfaceProtocol subclasses must implement "
        "void InterfaceAsync(const other_types_protocol_t* intf, interface_async_callback callback, void* cookie);");

    static_assert(internal::has_interface_protocol_async_refernce<D>::value,
        "InterfaceProtocol subclasses must implement "
        "void InterfaceAsyncRefernce(const other_types_protocol_t* intf, interface_async_refernce_callback callback, void* cookie);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_reference_protocol_struct, OtherTypesAsyncReferenceStruct,
        void (C::*)(const this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_reference_protocol_union, OtherTypesAsyncReferenceUnion,
        void (C::*)(const this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_reference_protocol_string, OtherTypesAsyncReferenceString,
        void (C::*)(const char* s, other_types_async_reference_string_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_reference_protocol_string_sized, OtherTypesAsyncReferenceStringSized,
        void (C::*)(const char* s, other_types_async_reference_string_sized_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_reference_protocol_string_sized2, OtherTypesAsyncReferenceStringSized2,
        void (C::*)(const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie));


template <typename D>
constexpr void CheckOtherTypesAsyncReferenceProtocolSubclass() {
    static_assert(internal::has_other_types_async_reference_protocol_struct<D>::value,
        "OtherTypesAsyncReferenceProtocol subclasses must implement "
        "void OtherTypesAsyncReferenceStruct(const this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_reference_protocol_union<D>::value,
        "OtherTypesAsyncReferenceProtocol subclasses must implement "
        "void OtherTypesAsyncReferenceUnion(const this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_reference_protocol_string<D>::value,
        "OtherTypesAsyncReferenceProtocol subclasses must implement "
        "void OtherTypesAsyncReferenceString(const char* s, other_types_async_reference_string_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_reference_protocol_string_sized<D>::value,
        "OtherTypesAsyncReferenceProtocol subclasses must implement "
        "void OtherTypesAsyncReferenceStringSized(const char* s, other_types_async_reference_string_sized_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_reference_protocol_string_sized2<D>::value,
        "OtherTypesAsyncReferenceProtocol subclasses must implement "
        "void OtherTypesAsyncReferenceStringSized2(const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_reference_protocol_struct, OtherTypesReferenceStruct,
        void (C::*)(const this_is_astruct_t* s, this_is_astruct_t** out_s));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_reference_protocol_union, OtherTypesReferenceUnion,
        void (C::*)(const this_is_aunion_t* u, this_is_aunion_t** out_u));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_reference_protocol_string, OtherTypesReferenceString,
        void (C::*)(const char* s, char* out_s, size_t s_capacity));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_reference_protocol_string_sized, OtherTypesReferenceStringSized,
        void (C::*)(const char* s, char* out_s, size_t s_capacity));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_reference_protocol_string_sized2, OtherTypesReferenceStringSized2,
        void (C::*)(const char* s, char* out_s, size_t s_capacity));


template <typename D>
constexpr void CheckOtherTypesReferenceProtocolSubclass() {
    static_assert(internal::has_other_types_reference_protocol_struct<D>::value,
        "OtherTypesReferenceProtocol subclasses must implement "
        "void OtherTypesReferenceStruct(const this_is_astruct_t* s, this_is_astruct_t** out_s);");

    static_assert(internal::has_other_types_reference_protocol_union<D>::value,
        "OtherTypesReferenceProtocol subclasses must implement "
        "void OtherTypesReferenceUnion(const this_is_aunion_t* u, this_is_aunion_t** out_u);");

    static_assert(internal::has_other_types_reference_protocol_string<D>::value,
        "OtherTypesReferenceProtocol subclasses must implement "
        "void OtherTypesReferenceString(const char* s, char* out_s, size_t s_capacity);");

    static_assert(internal::has_other_types_reference_protocol_string_sized<D>::value,
        "OtherTypesReferenceProtocol subclasses must implement "
        "void OtherTypesReferenceStringSized(const char* s, char* out_s, size_t s_capacity);");

    static_assert(internal::has_other_types_reference_protocol_string_sized2<D>::value,
        "OtherTypesReferenceProtocol subclasses must implement "
        "void OtherTypesReferenceStringSized2(const char* s, char* out_s, size_t s_capacity);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_protocol_struct, OtherTypesAsyncStruct,
        void (C::*)(const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_protocol_union, OtherTypesAsyncUnion,
        void (C::*)(const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_protocol_enum, OtherTypesAsyncEnum,
        void (C::*)(this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_protocol_string, OtherTypesAsyncString,
        void (C::*)(const char* s, other_types_async_string_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_protocol_string_sized, OtherTypesAsyncStringSized,
        void (C::*)(const char* s, other_types_async_string_sized_callback callback, void* cookie));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_async_protocol_string_sized2, OtherTypesAsyncStringSized2,
        void (C::*)(const char* s, other_types_async_string_sized2_callback callback, void* cookie));


template <typename D>
constexpr void CheckOtherTypesAsyncProtocolSubclass() {
    static_assert(internal::has_other_types_async_protocol_struct<D>::value,
        "OtherTypesAsyncProtocol subclasses must implement "
        "void OtherTypesAsyncStruct(const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_protocol_union<D>::value,
        "OtherTypesAsyncProtocol subclasses must implement "
        "void OtherTypesAsyncUnion(const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_protocol_enum<D>::value,
        "OtherTypesAsyncProtocol subclasses must implement "
        "void OtherTypesAsyncEnum(this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_protocol_string<D>::value,
        "OtherTypesAsyncProtocol subclasses must implement "
        "void OtherTypesAsyncString(const char* s, other_types_async_string_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_protocol_string_sized<D>::value,
        "OtherTypesAsyncProtocol subclasses must implement "
        "void OtherTypesAsyncStringSized(const char* s, other_types_async_string_sized_callback callback, void* cookie);");

    static_assert(internal::has_other_types_async_protocol_string_sized2<D>::value,
        "OtherTypesAsyncProtocol subclasses must implement "
        "void OtherTypesAsyncStringSized2(const char* s, other_types_async_string_sized2_callback callback, void* cookie);");

}

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_protocol_struct, OtherTypesStruct,
        void (C::*)(const this_is_astruct_t* s, this_is_astruct_t* out_s));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_protocol_union, OtherTypesUnion,
        void (C::*)(const this_is_aunion_t* u, this_is_aunion_t* out_u));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_protocol_enum, OtherTypesEnum,
        this_is_an_enum_t (C::*)(this_is_an_enum_t e));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_protocol_string, OtherTypesString,
        void (C::*)(const char* s, char* out_s, size_t s_capacity));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_protocol_string_sized, OtherTypesStringSized,
        void (C::*)(const char* s, char* out_s, size_t s_capacity));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_other_types_protocol_string_sized2, OtherTypesStringSized2,
        void (C::*)(const char* s, char* out_s, size_t s_capacity));


template <typename D>
constexpr void CheckOtherTypesProtocolSubclass() {
    static_assert(internal::has_other_types_protocol_struct<D>::value,
        "OtherTypesProtocol subclasses must implement "
        "void OtherTypesStruct(const this_is_astruct_t* s, this_is_astruct_t* out_s);");

    static_assert(internal::has_other_types_protocol_union<D>::value,
        "OtherTypesProtocol subclasses must implement "
        "void OtherTypesUnion(const this_is_aunion_t* u, this_is_aunion_t* out_u);");

    static_assert(internal::has_other_types_protocol_enum<D>::value,
        "OtherTypesProtocol subclasses must implement "
        "this_is_an_enum_t OtherTypesEnum(this_is_an_enum_t e);");

    static_assert(internal::has_other_types_protocol_string<D>::value,
        "OtherTypesProtocol subclasses must implement "
        "void OtherTypesString(const char* s, char* out_s, size_t s_capacity);");

    static_assert(internal::has_other_types_protocol_string_sized<D>::value,
        "OtherTypesProtocol subclasses must implement "
        "void OtherTypesStringSized(const char* s, char* out_s, size_t s_capacity);");

    static_assert(internal::has_other_types_protocol_string_sized2<D>::value,
        "OtherTypesProtocol subclasses must implement "
        "void OtherTypesStringSized2(const char* s, char* out_s, size_t s_capacity);");

}


} // namespace internal
} // namespace ddk
