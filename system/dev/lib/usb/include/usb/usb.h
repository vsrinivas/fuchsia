// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/usb.h>
#include <ddk/protocol/usb/composite.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb.h>
#ifdef __cplusplus
#include <ddktl/protocol/usb.h>
#endif

__BEGIN_CDECLS;

// helper function for claiming additional interfaces that satisfy the want_interface predicate,
// want_interface will be passed the supplied arg
zx_status_t usb_claim_additional_interfaces(
    usb_composite_protocol_t* comp,
    bool (*want_interface)(usb_interface_descriptor_t*, void*),
    void* arg);

// Utilities for iterating through descriptors within a device's USB configuration descriptor
typedef struct {
    uint8_t* desc;     // start of configuration descriptor
    uint8_t* desc_end; // end of configuration descriptor
    uint8_t* current;  // current position in configuration descriptor
} usb_desc_iter_t;

// initializes a usb_desc_iter_t
zx_status_t usb_desc_iter_init(usb_protocol_t* usb, usb_desc_iter_t* iter);

// Clones a usb_desc_iter_t
zx_status_t usb_desc_iter_clone(const usb_desc_iter_t* src, usb_desc_iter_t* dest);

// releases resources in a usb_desc_iter_t
void usb_desc_iter_release(usb_desc_iter_t* iter);

// resets iterator to the beginning
void usb_desc_iter_reset(usb_desc_iter_t* iter);

// returns the next descriptor
usb_descriptor_header_t* usb_desc_iter_next(usb_desc_iter_t* iter);

// returns the next descriptor without incrementing the iterator
usb_descriptor_header_t* usb_desc_iter_peek(usb_desc_iter_t* iter);

// returns the next interface descriptor, optionally skipping alternate interfaces
usb_interface_descriptor_t* usb_desc_iter_next_interface(usb_desc_iter_t* iter, bool skip_alt);

// returns the next endpoint descriptor within the current interface
usb_endpoint_descriptor_t* usb_desc_iter_next_endpoint(usb_desc_iter_t* iter);

static inline zx_status_t usb_get_descriptor(const usb_protocol_t* usb, uint8_t request_type,
                                             uint16_t type, uint16_t index, void* data,
                                             size_t length, zx_time_t timeout, size_t* out_length) {
    return usb_control_in(usb, request_type | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                          (uint16_t)(type << 8 | index), 0, timeout, data, length, out_length);
}

static inline zx_status_t usb_get_status(const usb_protocol_t* usb, uint8_t request_type,
                                         uint16_t index, void* data, size_t length,
                                         zx_time_t timeout, size_t* out_length) {
    return usb_control_in(usb, request_type | USB_DIR_IN, USB_REQ_GET_STATUS, 0, index, timeout,
                          data, length, out_length);
}

static inline zx_status_t usb_set_feature(const usb_protocol_t* usb, uint8_t request_type,
                                          uint16_t feature, uint16_t index, zx_time_t timeout) {
    return usb_control_out(usb, request_type, USB_REQ_SET_FEATURE, feature, index, timeout,
                           NULL, 0);
}

static inline zx_status_t usb_clear_feature(const usb_protocol_t* usb, uint8_t request_type,
                                            uint16_t feature, uint16_t index, zx_time_t timeout) {
    return usb_control_out(usb, request_type, USB_REQ_CLEAR_FEATURE, feature, index, timeout,
                           NULL, 0);
}

__END_CDECLS;

#ifdef __cplusplus
namespace usb {

// Usage and implementation notes
// Interface is owned by an iterator of an InterfaceList.
// It is possible to enumerate all USB endpoint descriptors
// by using the standard iterator interface, or a for loop
// such as for (auto endpoint:interface) { ... }
// where interface is an instance of an Interface.
// Interfaces must not outlive their original InterfaceLists.
class Interface {
private:
    class iterator_impl;

public:
    using const_iterator = iterator_impl;

    const usb_interface_descriptor_t* descriptor() const {
        return descriptor_;
    }

    const_iterator begin() const;

    const const_iterator cbegin() const;

    const_iterator end() const;

    const const_iterator cend() const;

    friend class InterfaceList;

private:
    class iterator_impl {
    public:
        iterator_impl(const usb_desc_iter_t& iter);

        bool operator==(const iterator_impl& other) const {
            return endpoint_ == other.endpoint_;
        }

        bool operator!=(const iterator_impl& other) const {
            return endpoint_ != other.endpoint_;
        }

        iterator_impl operator++(int);

        iterator_impl& operator++();

        const usb_endpoint_descriptor_t* endpoint() const {
            return endpoint_;
        }

        const usb_endpoint_descriptor_t& operator*() const {
            return *endpoint_;
        }

        const usb_endpoint_descriptor_t* operator->() const {
            return endpoint_;
        }

    private:
        const usb_endpoint_descriptor_t* endpoint_;

        usb_desc_iter_t iter_;
    };

    Interface() {}

    Interface(const usb_desc_iter_t& iter)
        : iter_(iter), descriptor_(nullptr) {
    }

    void Next(bool skip_alt);

    usb_desc_iter_t iter_;

    const usb_interface_descriptor_t* descriptor_;
};

// Usage and implementation notes
// An InterfaceList can be used for enumerating USB interfaces and endpoints.
// It implements a standard C++ iterator interface, which can be used with a for loop.
// such as for(interface:interface_list) { ... }
// The InterfaceList constructor takes in a UsbProtocolClient as a parameter,
// and a boolean skip_alt parameter.
// The InterfaceList will enumerate interfaces in the client,
//  and will skip any alternate interfaces if skip_alt is true.
// (see page 268 of the USB 2.0 specification for more information)
// The constructor may fail in the event of a memory allocation failure or other protocol error.
// After constructing this InterfaceList,
// it is recommended to use .check() to verify the operation succeeded.
// If check returns an error, it is still safe to call the iteration functions,
// but the resulting enumeration will return no values.
class InterfaceList {
private:
    class iterator_impl;

public:
    using const_iterator = iterator_impl;

    const_iterator begin() const;

    const const_iterator cbegin() const;

    const_iterator end() const;

    const const_iterator cend() const;

    InterfaceList(const ddk::UsbProtocolClient& client, bool skip_alt = false);

    zx_status_t check() const;

    ~InterfaceList();

private:
    class iterator_impl {
    public:
        iterator_impl(const usb_desc_iter_t& iter, bool skip_alt_);

        bool operator==(const iterator_impl& other) const {
            return interface_.descriptor_ == other.interface_.descriptor_;
        }

        bool operator!=(const iterator_impl& other) const {
            return interface_.descriptor_ != other.interface_.descriptor_;
        }

        iterator_impl operator++(int);

        iterator_impl& operator++();

        const Interface* get() const {
            return &interface_;
        }

        const Interface& operator*() const {
            return interface_;
        }

        const Interface* operator->() const {
            return &interface_;
        }

    private:
        Interface interface_;

        const bool skip_alt_;
    };

    const bool skip_alt_;

    zx_status_t status_;

    usb_desc_iter_t iter_;
};
} // namespace usb
#endif