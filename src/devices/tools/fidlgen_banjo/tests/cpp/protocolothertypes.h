// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocolothertypes banjo file

#pragma once

#include <banjo/examples/protocolothertypes/c/banjo.h>
#include <ddktl/device-internal.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "banjo-internal.h"

// DDK protocolothertypes-protocol support
//
// :: Proxies ::
//
// ddk::OtherTypesReferenceProtocolClient is a simple wrapper around
// other_types_reference_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::OtherTypesReferenceProtocol is a mixin class that simplifies writing DDK drivers
// that implement the other-types-reference protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_OTHER_TYPES_REFERENCE device.
// class OtherTypesReferenceDevice;
// using OtherTypesReferenceDeviceType = ddk::Device<OtherTypesReferenceDevice, /* ddk mixins */>;
//
// class OtherTypesReferenceDevice : public OtherTypesReferenceDeviceType,
//                      public ddk::OtherTypesReferenceProtocol<OtherTypesReferenceDevice> {
//   public:
//     OtherTypesReferenceDevice(zx_device_t* parent)
//         : OtherTypesReferenceDeviceType(parent) {}
//
//     void OtherTypesReferenceStruct(const this_is_astruct_t* s, this_is_astruct_t** out_s);
//
//     void OtherTypesReferenceUnion(const this_is_aunion_t* u, this_is_aunion_t** out_u);
//
//     void OtherTypesReferenceString(const char* s, char* out_s, size_t s_capacity);
//
//     void OtherTypesReferenceStringSized(const char* s, char* out_s, size_t s_capacity);
//
//     void OtherTypesReferenceStringSized2(const char* s, char* out_s, size_t s_capacity);
//
//     ...
// };
// :: Proxies ::
//
// ddk::OtherTypesProtocolClient is a simple wrapper around
// other_types_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::OtherTypesProtocol is a mixin class that simplifies writing DDK drivers
// that implement the other-types protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_OTHER_TYPES device.
// class OtherTypesDevice;
// using OtherTypesDeviceType = ddk::Device<OtherTypesDevice, /* ddk mixins */>;
//
// class OtherTypesDevice : public OtherTypesDeviceType,
//                      public ddk::OtherTypesProtocol<OtherTypesDevice> {
//   public:
//     OtherTypesDevice(zx_device_t* parent)
//         : OtherTypesDeviceType(parent) {}
//
//     void OtherTypesStruct(const this_is_astruct_t* s, this_is_astruct_t* out_s);
//
//     void OtherTypesUnion(const this_is_aunion_t* u, this_is_aunion_t* out_u);
//
//     this_is_an_enum_t OtherTypesEnum(this_is_an_enum_t e);
//
//     this_is_abits_t OtherTypesBits(this_is_abits_t e);
//
//     void OtherTypesString(const char* s, char* out_s, size_t s_capacity);
//
//     void OtherTypesStringSized(const char* s, char* out_s, size_t s_capacity);
//
//     void OtherTypesStringSized2(const char* s, char* out_s, size_t s_capacity);
//
//     uint32_t OtherTypesInlineTable(uint32_t request_member);
//
//     ...
// };
// :: Proxies ::
//
// ddk::OtherTypesAsyncProtocolClient is a simple wrapper around
// other_types_async_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::OtherTypesAsyncProtocol is a mixin class that simplifies writing DDK drivers
// that implement the other-types-async protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_OTHER_TYPES_ASYNC device.
// class OtherTypesAsyncDevice;
// using OtherTypesAsyncDeviceType = ddk::Device<OtherTypesAsyncDevice, /* ddk mixins */>;
//
// class OtherTypesAsyncDevice : public OtherTypesAsyncDeviceType,
//                      public ddk::OtherTypesAsyncProtocol<OtherTypesAsyncDevice> {
//   public:
//     OtherTypesAsyncDevice(zx_device_t* parent)
//         : OtherTypesAsyncDeviceType(parent) {}
//
//     void OtherTypesAsyncStruct(const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie);
//
//     void OtherTypesAsyncUnion(const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie);
//
//     void OtherTypesAsyncEnum(this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie);
//
//     void OtherTypesAsyncBits(this_is_abits_t e, other_types_async_bits_callback callback, void* cookie);
//
//     void OtherTypesAsyncString(const char* s, other_types_async_string_callback callback, void* cookie);
//
//     void OtherTypesAsyncStringSized(const char* s, other_types_async_string_sized_callback callback, void* cookie);
//
//     void OtherTypesAsyncStringSized2(const char* s, other_types_async_string_sized2_callback callback, void* cookie);
//
//     ...
// };
// :: Proxies ::
//
// ddk::OtherTypesAsyncReferenceProtocolClient is a simple wrapper around
// other_types_async_reference_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::OtherTypesAsyncReferenceProtocol is a mixin class that simplifies writing DDK drivers
// that implement the other-types-async-reference protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_OTHER_TYPES_ASYNC_REFERENCE device.
// class OtherTypesAsyncReferenceDevice;
// using OtherTypesAsyncReferenceDeviceType = ddk::Device<OtherTypesAsyncReferenceDevice, /* ddk mixins */>;
//
// class OtherTypesAsyncReferenceDevice : public OtherTypesAsyncReferenceDeviceType,
//                      public ddk::OtherTypesAsyncReferenceProtocol<OtherTypesAsyncReferenceDevice> {
//   public:
//     OtherTypesAsyncReferenceDevice(zx_device_t* parent)
//         : OtherTypesAsyncReferenceDeviceType(parent) {}
//
//     void OtherTypesAsyncReferenceStruct(const this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie);
//
//     void OtherTypesAsyncReferenceUnion(const this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie);
//
//     void OtherTypesAsyncReferenceString(const char* s, other_types_async_reference_string_callback callback, void* cookie);
//
//     void OtherTypesAsyncReferenceStringSized(const char* s, other_types_async_reference_string_sized_callback callback, void* cookie);
//
//     void OtherTypesAsyncReferenceStringSized2(const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie);
//
//     ...
// };
// :: Proxies ::
//
// ddk::InterfaceProtocolClient is a simple wrapper around
// interface_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::InterfaceProtocol is a mixin class that simplifies writing DDK drivers
// that implement the interface protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_INTERFACE device.
// class InterfaceDevice;
// using InterfaceDeviceType = ddk::Device<InterfaceDevice, /* ddk mixins */>;
//
// class InterfaceDevice : public InterfaceDeviceType,
//                      public ddk::InterfaceProtocol<InterfaceDevice> {
//   public:
//     InterfaceDevice(zx_device_t* parent)
//         : InterfaceDeviceType(parent) {}
//
//     void InterfaceValue(const other_types_protocol_t* intf, other_types_protocol_t* out_intf);
//
//     void InterfaceReference(const other_types_protocol_t* intf, other_types_protocol_t** out_intf);
//
//     void InterfaceAsync(const other_types_protocol_t* intf, interface_async_callback callback, void* cookie);
//
//     void InterfaceAsyncRefernce(const other_types_protocol_t* intf, interface_async_refernce_callback callback, void* cookie);
//
//     ...
// };

namespace ddk {

template <typename D, typename Base = internal::base_mixin>
class OtherTypesReferenceProtocol : public Base {
public:
    OtherTypesReferenceProtocol() {
        internal::CheckOtherTypesReferenceProtocolSubclass<D>();
        other_types_reference_protocol_ops_.struct = OtherTypesReferenceStruct;
        other_types_reference_protocol_ops_.union = OtherTypesReferenceUnion;
        other_types_reference_protocol_ops_.string = OtherTypesReferenceString;
        other_types_reference_protocol_ops_.string_sized = OtherTypesReferenceStringSized;
        other_types_reference_protocol_ops_.string_sized2 = OtherTypesReferenceStringSized2;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_OTHER_TYPES_REFERENCE;
            dev->ddk_proto_ops_ = &other_types_reference_protocol_ops_;
        }
    }

protected:
    other_types_reference_protocol_ops_t other_types_reference_protocol_ops_ = {};

private:
    static void OtherTypesReferenceStruct(void* ctx, const this_is_astruct_t* s, this_is_astruct_t** out_s) {
        static_cast<D*>(ctx)->OtherTypesReferenceStruct(s, out_s);
    }
    static void OtherTypesReferenceUnion(void* ctx, const this_is_aunion_t* u, this_is_aunion_t** out_u) {
        static_cast<D*>(ctx)->OtherTypesReferenceUnion(u, out_u);
    }
    static void OtherTypesReferenceString(void* ctx, const char* s, char* out_s, size_t s_capacity) {
        static_cast<D*>(ctx)->OtherTypesReferenceString(s, out_s, s_capacity);
    }
    static void OtherTypesReferenceStringSized(void* ctx, const char* s, char* out_s, size_t s_capacity) {
        static_cast<D*>(ctx)->OtherTypesReferenceStringSized(s, out_s, s_capacity);
    }
    static void OtherTypesReferenceStringSized2(void* ctx, const char* s, char* out_s, size_t s_capacity) {
        static_cast<D*>(ctx)->OtherTypesReferenceStringSized2(s, out_s, s_capacity);
    }
};

class OtherTypesReferenceProtocolClient {
public:
    OtherTypesReferenceProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    OtherTypesReferenceProtocolClient(const other_types_reference_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    OtherTypesReferenceProtocolClient(zx_device_t* parent) {
        other_types_reference_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_OTHER_TYPES_REFERENCE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    OtherTypesReferenceProtocolClient(zx_device_t* parent, const char* fragment_name) {
        other_types_reference_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_OTHER_TYPES_REFERENCE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a OtherTypesReferenceProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        OtherTypesReferenceProtocolClient* result) {
        other_types_reference_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_OTHER_TYPES_REFERENCE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = OtherTypesReferenceProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a OtherTypesReferenceProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        OtherTypesReferenceProtocolClient* result) {
        other_types_reference_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_OTHER_TYPES_REFERENCE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = OtherTypesReferenceProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(other_types_reference_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void Struct(const this_is_astruct_t* s, this_is_astruct_t** out_s) const {
        ops_->struct(ctx_, s, out_s);
    }

    void Union(const this_is_aunion_t* u, this_is_aunion_t** out_u) const {
        ops_->union(ctx_, u, out_u);
    }

    void String(const char* s, char* out_s, size_t s_capacity) const {
        ops_->string(ctx_, s, out_s, s_capacity);
    }

    void StringSized(const char* s, char* out_s, size_t s_capacity) const {
        ops_->string_sized(ctx_, s, out_s, s_capacity);
    }

    void StringSized2(const char* s, char* out_s, size_t s_capacity) const {
        ops_->string_sized2(ctx_, s, out_s, s_capacity);
    }

private:
    const other_types_reference_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class OtherTypesProtocol : public Base {
public:
    OtherTypesProtocol() {
        internal::CheckOtherTypesProtocolSubclass<D>();
        other_types_protocol_ops_.struct = OtherTypesStruct;
        other_types_protocol_ops_.union = OtherTypesUnion;
        other_types_protocol_ops_.enum = OtherTypesEnum;
        other_types_protocol_ops_.bits = OtherTypesBits;
        other_types_protocol_ops_.string = OtherTypesString;
        other_types_protocol_ops_.string_sized = OtherTypesStringSized;
        other_types_protocol_ops_.string_sized2 = OtherTypesStringSized2;
        other_types_protocol_ops_.inline_table = OtherTypesInlineTable;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_OTHER_TYPES;
            dev->ddk_proto_ops_ = &other_types_protocol_ops_;
        }
    }

protected:
    other_types_protocol_ops_t other_types_protocol_ops_ = {};

private:
    static void OtherTypesStruct(void* ctx, const this_is_astruct_t* s, this_is_astruct_t* out_s) {
        static_cast<D*>(ctx)->OtherTypesStruct(s, out_s);
    }
    static void OtherTypesUnion(void* ctx, const this_is_aunion_t* u, this_is_aunion_t* out_u) {
        static_cast<D*>(ctx)->OtherTypesUnion(u, out_u);
    }
    static this_is_an_enum_t OtherTypesEnum(void* ctx, this_is_an_enum_t e) {
        auto ret = static_cast<D*>(ctx)->OtherTypesEnum(e);
        return ret;
    }
    static this_is_abits_t OtherTypesBits(void* ctx, this_is_abits_t e) {
        auto ret = static_cast<D*>(ctx)->OtherTypesBits(e);
        return ret;
    }
    static void OtherTypesString(void* ctx, const char* s, char* out_s, size_t s_capacity) {
        static_cast<D*>(ctx)->OtherTypesString(s, out_s, s_capacity);
    }
    static void OtherTypesStringSized(void* ctx, const char* s, char* out_s, size_t s_capacity) {
        static_cast<D*>(ctx)->OtherTypesStringSized(s, out_s, s_capacity);
    }
    static void OtherTypesStringSized2(void* ctx, const char* s, char* out_s, size_t s_capacity) {
        static_cast<D*>(ctx)->OtherTypesStringSized2(s, out_s, s_capacity);
    }
    static uint32_t OtherTypesInlineTable(void* ctx, uint32_t request_member) {
        auto ret = static_cast<D*>(ctx)->OtherTypesInlineTable(request_member);
        return ret;
    }
};

class OtherTypesProtocolClient {
public:
    OtherTypesProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    OtherTypesProtocolClient(const other_types_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    OtherTypesProtocolClient(zx_device_t* parent) {
        other_types_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_OTHER_TYPES, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    OtherTypesProtocolClient(zx_device_t* parent, const char* fragment_name) {
        other_types_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_OTHER_TYPES, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a OtherTypesProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        OtherTypesProtocolClient* result) {
        other_types_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_OTHER_TYPES, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = OtherTypesProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a OtherTypesProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        OtherTypesProtocolClient* result) {
        other_types_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_OTHER_TYPES, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = OtherTypesProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(other_types_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void Struct(const this_is_astruct_t* s, this_is_astruct_t* out_s) const {
        ops_->struct(ctx_, s, out_s);
    }

    void Union(const this_is_aunion_t* u, this_is_aunion_t* out_u) const {
        ops_->union(ctx_, u, out_u);
    }

    this_is_an_enum_t Enum(this_is_an_enum_t e) const {
        return ops_->enum(ctx_, e);
    }

    this_is_abits_t Bits(this_is_abits_t e) const {
        return ops_->bits(ctx_, e);
    }

    void String(const char* s, char* out_s, size_t s_capacity) const {
        ops_->string(ctx_, s, out_s, s_capacity);
    }

    void StringSized(const char* s, char* out_s, size_t s_capacity) const {
        ops_->string_sized(ctx_, s, out_s, s_capacity);
    }

    void StringSized2(const char* s, char* out_s, size_t s_capacity) const {
        ops_->string_sized2(ctx_, s, out_s, s_capacity);
    }

    uint32_t InlineTable(uint32_t request_member) const {
        return ops_->inline_table(ctx_, request_member);
    }

private:
    const other_types_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class OtherTypesAsyncProtocol : public Base {
public:
    OtherTypesAsyncProtocol() {
        internal::CheckOtherTypesAsyncProtocolSubclass<D>();
        other_types_async_protocol_ops_.struct = OtherTypesAsyncStruct;
        other_types_async_protocol_ops_.union = OtherTypesAsyncUnion;
        other_types_async_protocol_ops_.enum = OtherTypesAsyncEnum;
        other_types_async_protocol_ops_.bits = OtherTypesAsyncBits;
        other_types_async_protocol_ops_.string = OtherTypesAsyncString;
        other_types_async_protocol_ops_.string_sized = OtherTypesAsyncStringSized;
        other_types_async_protocol_ops_.string_sized2 = OtherTypesAsyncStringSized2;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_OTHER_TYPES_ASYNC;
            dev->ddk_proto_ops_ = &other_types_async_protocol_ops_;
        }
    }

protected:
    other_types_async_protocol_ops_t other_types_async_protocol_ops_ = {};

private:
    static void OtherTypesAsyncStruct(void* ctx, const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncStruct(s, callback, cookie);
    }
    static void OtherTypesAsyncUnion(void* ctx, const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncUnion(u, callback, cookie);
    }
    static void OtherTypesAsyncEnum(void* ctx, this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncEnum(e, callback, cookie);
    }
    static void OtherTypesAsyncBits(void* ctx, this_is_abits_t e, other_types_async_bits_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncBits(e, callback, cookie);
    }
    static void OtherTypesAsyncString(void* ctx, const char* s, other_types_async_string_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncString(s, callback, cookie);
    }
    static void OtherTypesAsyncStringSized(void* ctx, const char* s, other_types_async_string_sized_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncStringSized(s, callback, cookie);
    }
    static void OtherTypesAsyncStringSized2(void* ctx, const char* s, other_types_async_string_sized2_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncStringSized2(s, callback, cookie);
    }
};

class OtherTypesAsyncProtocolClient {
public:
    OtherTypesAsyncProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    OtherTypesAsyncProtocolClient(const other_types_async_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    OtherTypesAsyncProtocolClient(zx_device_t* parent) {
        other_types_async_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_OTHER_TYPES_ASYNC, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    OtherTypesAsyncProtocolClient(zx_device_t* parent, const char* fragment_name) {
        other_types_async_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_OTHER_TYPES_ASYNC, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a OtherTypesAsyncProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        OtherTypesAsyncProtocolClient* result) {
        other_types_async_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_OTHER_TYPES_ASYNC, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = OtherTypesAsyncProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a OtherTypesAsyncProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        OtherTypesAsyncProtocolClient* result) {
        other_types_async_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_OTHER_TYPES_ASYNC, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = OtherTypesAsyncProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(other_types_async_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void Struct(const this_is_astruct_t* s, other_types_async_struct_callback callback, void* cookie) const {
        ops_->struct(ctx_, s, callback, cookie);
    }

    void Union(const this_is_aunion_t* u, other_types_async_union_callback callback, void* cookie) const {
        ops_->union(ctx_, u, callback, cookie);
    }

    void Enum(this_is_an_enum_t e, other_types_async_enum_callback callback, void* cookie) const {
        ops_->enum(ctx_, e, callback, cookie);
    }

    void Bits(this_is_abits_t e, other_types_async_bits_callback callback, void* cookie) const {
        ops_->bits(ctx_, e, callback, cookie);
    }

    void String(const char* s, other_types_async_string_callback callback, void* cookie) const {
        ops_->string(ctx_, s, callback, cookie);
    }

    void StringSized(const char* s, other_types_async_string_sized_callback callback, void* cookie) const {
        ops_->string_sized(ctx_, s, callback, cookie);
    }

    void StringSized2(const char* s, other_types_async_string_sized2_callback callback, void* cookie) const {
        ops_->string_sized2(ctx_, s, callback, cookie);
    }

private:
    const other_types_async_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class OtherTypesAsyncReferenceProtocol : public Base {
public:
    OtherTypesAsyncReferenceProtocol() {
        internal::CheckOtherTypesAsyncReferenceProtocolSubclass<D>();
        other_types_async_reference_protocol_ops_.struct = OtherTypesAsyncReferenceStruct;
        other_types_async_reference_protocol_ops_.union = OtherTypesAsyncReferenceUnion;
        other_types_async_reference_protocol_ops_.string = OtherTypesAsyncReferenceString;
        other_types_async_reference_protocol_ops_.string_sized = OtherTypesAsyncReferenceStringSized;
        other_types_async_reference_protocol_ops_.string_sized2 = OtherTypesAsyncReferenceStringSized2;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_OTHER_TYPES_ASYNC_REFERENCE;
            dev->ddk_proto_ops_ = &other_types_async_reference_protocol_ops_;
        }
    }

protected:
    other_types_async_reference_protocol_ops_t other_types_async_reference_protocol_ops_ = {};

private:
    static void OtherTypesAsyncReferenceStruct(void* ctx, const this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncReferenceStruct(s, callback, cookie);
    }
    static void OtherTypesAsyncReferenceUnion(void* ctx, const this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncReferenceUnion(u, callback, cookie);
    }
    static void OtherTypesAsyncReferenceString(void* ctx, const char* s, other_types_async_reference_string_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncReferenceString(s, callback, cookie);
    }
    static void OtherTypesAsyncReferenceStringSized(void* ctx, const char* s, other_types_async_reference_string_sized_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncReferenceStringSized(s, callback, cookie);
    }
    static void OtherTypesAsyncReferenceStringSized2(void* ctx, const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie) {
        static_cast<D*>(ctx)->OtherTypesAsyncReferenceStringSized2(s, callback, cookie);
    }
};

class OtherTypesAsyncReferenceProtocolClient {
public:
    OtherTypesAsyncReferenceProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    OtherTypesAsyncReferenceProtocolClient(const other_types_async_reference_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    OtherTypesAsyncReferenceProtocolClient(zx_device_t* parent) {
        other_types_async_reference_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_OTHER_TYPES_ASYNC_REFERENCE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    OtherTypesAsyncReferenceProtocolClient(zx_device_t* parent, const char* fragment_name) {
        other_types_async_reference_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_OTHER_TYPES_ASYNC_REFERENCE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a OtherTypesAsyncReferenceProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        OtherTypesAsyncReferenceProtocolClient* result) {
        other_types_async_reference_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_OTHER_TYPES_ASYNC_REFERENCE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = OtherTypesAsyncReferenceProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a OtherTypesAsyncReferenceProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        OtherTypesAsyncReferenceProtocolClient* result) {
        other_types_async_reference_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_OTHER_TYPES_ASYNC_REFERENCE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = OtherTypesAsyncReferenceProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(other_types_async_reference_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void Struct(const this_is_astruct_t* s, other_types_async_reference_struct_callback callback, void* cookie) const {
        ops_->struct(ctx_, s, callback, cookie);
    }

    void Union(const this_is_aunion_t* u, other_types_async_reference_union_callback callback, void* cookie) const {
        ops_->union(ctx_, u, callback, cookie);
    }

    void String(const char* s, other_types_async_reference_string_callback callback, void* cookie) const {
        ops_->string(ctx_, s, callback, cookie);
    }

    void StringSized(const char* s, other_types_async_reference_string_sized_callback callback, void* cookie) const {
        ops_->string_sized(ctx_, s, callback, cookie);
    }

    void StringSized2(const char* s, other_types_async_reference_string_sized2_callback callback, void* cookie) const {
        ops_->string_sized2(ctx_, s, callback, cookie);
    }

private:
    const other_types_async_reference_protocol_ops_t* ops_;
    void* ctx_;
};

template <typename D, typename Base = internal::base_mixin>
class InterfaceProtocol : public Base {
public:
    InterfaceProtocol() {
        internal::CheckInterfaceProtocolSubclass<D>();
        interface_protocol_ops_.value = InterfaceValue;
        interface_protocol_ops_.reference = InterfaceReference;
        interface_protocol_ops_.async = InterfaceAsync;
        interface_protocol_ops_.async_refernce = InterfaceAsyncRefernce;

        if constexpr (internal::is_base_proto<Base>::value) {
            auto dev = static_cast<D*>(this);
            // Can only inherit from one base_protocol implementation.
            ZX_ASSERT(dev->ddk_proto_id_ == 0);
            dev->ddk_proto_id_ = ZX_PROTOCOL_INTERFACE;
            dev->ddk_proto_ops_ = &interface_protocol_ops_;
        }
    }

protected:
    interface_protocol_ops_t interface_protocol_ops_ = {};

private:
    static void InterfaceValue(void* ctx, const other_types_protocol_t* intf, other_types_protocol_t* out_intf) {
        static_cast<D*>(ctx)->InterfaceValue(intf, out_intf);
    }
    static void InterfaceReference(void* ctx, const other_types_protocol_t* intf, other_types_protocol_t** out_intf) {
        static_cast<D*>(ctx)->InterfaceReference(intf, out_intf);
    }
    static void InterfaceAsync(void* ctx, const other_types_protocol_t* intf, interface_async_callback callback, void* cookie) {
        static_cast<D*>(ctx)->InterfaceAsync(intf, callback, cookie);
    }
    static void InterfaceAsyncRefernce(void* ctx, const other_types_protocol_t* intf, interface_async_refernce_callback callback, void* cookie) {
        static_cast<D*>(ctx)->InterfaceAsyncRefernce(intf, callback, cookie);
    }
};

class InterfaceProtocolClient {
public:
    InterfaceProtocolClient()
        : ops_(nullptr), ctx_(nullptr) {}
    InterfaceProtocolClient(const interface_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    InterfaceProtocolClient(zx_device_t* parent) {
        interface_protocol_t proto;
        if (device_get_protocol(parent, ZX_PROTOCOL_INTERFACE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    InterfaceProtocolClient(zx_device_t* parent, const char* fragment_name) {
        interface_protocol_t proto;
        if (device_get_fragment_protocol(parent, fragment_name, ZX_PROTOCOL_INTERFACE, &proto) == ZX_OK) {
            ops_ = proto.ops;
            ctx_ = proto.ctx;
        } else {
            ops_ = nullptr;
            ctx_ = nullptr;
        }
    }

    // Create a InterfaceProtocolClient from the given parent device + "fragment".
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent,
                                        InterfaceProtocolClient* result) {
        interface_protocol_t proto;
        zx_status_t status = device_get_protocol(
                parent, ZX_PROTOCOL_INTERFACE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = InterfaceProtocolClient(&proto);
        return ZX_OK;
    }

    // Create a InterfaceProtocolClient from the given parent device.
    //
    // If ZX_OK is returned, the created object will be initialized in |result|.
    static zx_status_t CreateFromDevice(zx_device_t* parent, const char* fragment_name,
                                        InterfaceProtocolClient* result) {
        interface_protocol_t proto;
        zx_status_t status = device_get_fragment_protocol(parent, fragment_name,
                                 ZX_PROTOCOL_INTERFACE, &proto);
        if (status != ZX_OK) {
            return status;
        }
        *result = InterfaceProtocolClient(&proto);
        return ZX_OK;
    }

    void GetProto(interface_protocol_t* proto) const {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() const {
        return ops_ != nullptr;
    }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }

    void Value(void* intf_ctx, const other_types_protocol_ops_t* intf_ops, other_types_protocol_t* out_intf) const {
        const other_types_protocol_t intf2 = {
            .ops = intf_ops,
            .ctx = intf_ctx,
        };
        const other_types_protocol_t* intf = &intf2;
        ops_->value(ctx_, intf, out_intf);
    }

    void Reference(void* intf_ctx, const other_types_protocol_ops_t* intf_ops, other_types_protocol_t** out_intf) const {
        const other_types_protocol_t intf2 = {
            .ops = intf_ops,
            .ctx = intf_ctx,
        };
        const other_types_protocol_t* intf = &intf2;
        ops_->reference(ctx_, intf, out_intf);
    }

    void Async(void* intf_ctx, const other_types_protocol_ops_t* intf_ops, interface_async_callback callback, void* cookie) const {
        const other_types_protocol_t intf2 = {
            .ops = intf_ops,
            .ctx = intf_ctx,
        };
        const other_types_protocol_t* intf = &intf2;
        ops_->async(ctx_, intf, callback, cookie);
    }

    void AsyncRefernce(void* intf_ctx, const other_types_protocol_ops_t* intf_ops, interface_async_refernce_callback callback, void* cookie) const {
        const other_types_protocol_t intf2 = {
            .ops = intf_ops,
            .ctx = intf_ctx,
        };
        const other_types_protocol_t* intf = &intf2;
        ops_->async_refernce(ctx_, intf, callback, cookie);
    }

private:
    const interface_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
