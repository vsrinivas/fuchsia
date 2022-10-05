// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_USB_H_
#define SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_USB_H_

#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/composite/c/banjo.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb.h>
#ifdef __cplusplus
#include <fuchsia/hardware/usb/cpp/banjo.h>

#include <optional>
#endif

__BEGIN_CDECLS

// helper function for claiming additional interfaces that satisfy the want_interface predicate,
// want_interface will be passed the supplied arg
// clang-format off
zx_status_t usb_claim_additional_interfaces(
  usb_composite_protocol_t* comp,
  bool (*want_interface)(usb_interface_descriptor_t*, void*),
  void* arg);
// clang-format on

// Utilities for iterating through descriptors within a device's USB configuration descriptor
typedef struct {
  uint8_t* desc;      // start of configuration descriptor
  uint8_t* desc_end;  // end of configuration descriptor
  uint8_t* current;   // current position in configuration descriptor
} usb_desc_iter_t;

// initializes a usb_desc_iter_t
zx_status_t usb_desc_iter_init(usb_protocol_t* usb, usb_desc_iter_t* iter);

// initializes a usb_desc_iter_t. Doesn't allocate any memory (iterator doesn't need to be released)
zx_status_t usb_desc_iter_init_unowned(void* descriptors, size_t length, usb_desc_iter_t* iter);

// Clones a usb_desc_iter_t
zx_status_t usb_desc_iter_clone(const usb_desc_iter_t* src, usb_desc_iter_t* dest);

// releases resources in a usb_desc_iter_t
void usb_desc_iter_release(usb_desc_iter_t* iter);

// resets iterator to the beginning
void usb_desc_iter_reset(usb_desc_iter_t* iter);

// returns the descriptor header structure currently pointed by the iterator. If the current
// iterator does not point to a valid descriptor header structure, NULL would be returned and user
// is expected to handle the error case and end the descriptor parsing.
usb_descriptor_header_t* usb_desc_iter_peek(usb_desc_iter_t* iter);

// increase the iterator to the next descriptor. If the current descriptor is not a valid descriptor
// header structure, returns false, otherwise, returns true. The iterator would not be increased
// if false is returned and user is expected to handle the error case and end the descriptor
// parsing.
bool usb_desc_iter_advance(usb_desc_iter_t* iter);

// returns the expected structure with structure size currently pointed by the iterator. If the
// length of descriptor buffer current pointed by the iterator is not enough to hold the structure,
// NULL would be returned, user is expected to handle the error case.
void* usb_desc_iter_get_structure(usb_desc_iter_t* iter, size_t structure_size);

// returns the next interface descriptor, optionally skipping alternate interfaces
usb_interface_descriptor_t* usb_desc_iter_next_interface(usb_desc_iter_t* iter, bool skip_alt);

// returns the next endpoint descriptor within the current interface
usb_endpoint_descriptor_t* usb_desc_iter_next_endpoint(usb_desc_iter_t* iter);

// returns the next ss-companion descriptor within the current interface
usb_ss_ep_comp_descriptor_t* usb_desc_iter_next_ss_ep_comp(usb_desc_iter_t* iter);

static inline zx_status_t usb_get_descriptor(const usb_protocol_t* usb, uint8_t request_type,
                                             uint16_t type, uint16_t index, uint8_t* data,
                                             size_t length, zx_time_t timeout, size_t* out_length) {
  return usb_control_in(usb, request_type | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (uint16_t)(type << 8 | index), 0, timeout, data, length, out_length);
}

static inline zx_status_t usb_get_status(const usb_protocol_t* usb, uint8_t request_type,
                                         uint16_t index, void* data, size_t length,
                                         zx_time_t timeout, size_t* out_length) {
  return usb_control_in(usb, request_type | USB_DIR_IN, USB_REQ_GET_STATUS, 0, index, timeout,
                        (uint8_t*)data, length, out_length);
}

static inline zx_status_t usb_set_feature(const usb_protocol_t* usb, uint8_t request_type,
                                          uint16_t feature, uint16_t index, zx_time_t timeout) {
  return usb_control_out(usb, request_type, USB_REQ_SET_FEATURE, feature, index, timeout, NULL, 0);
}

static inline zx_status_t usb_clear_feature(const usb_protocol_t* usb, uint8_t request_type,
                                            uint16_t feature, uint16_t index, zx_time_t timeout) {
  return usb_control_out(usb, request_type, USB_REQ_CLEAR_FEATURE, feature, index, timeout, NULL,
                         0);
}

// Descriptor support macros.
#define usb_ep_num(ep) ((ep)->b_endpoint_address & USB_ENDPOINT_NUM_MASK)
// usb_ep_num2() useful with you have b_endpoint_address outside of a descriptor.
#define usb_ep_num2(addr) ((addr)&USB_ENDPOINT_NUM_MASK)
#define usb_ep_direction(ep) ((ep)->b_endpoint_address & USB_ENDPOINT_DIR_MASK)
// usb_ep_direction2() useful with you have b_endpoint_address outside of a descriptor.
#define usb_ep_direction2(addr) ((addr)&USB_ENDPOINT_DIR_MASK)
#define usb_ep_type(ep) ((ep)->bm_attributes & USB_ENDPOINT_TYPE_MASK)
#define usb_ep_sync_type(ep) ((ep)->bm_attributes & USB_ENDPOINT_SYNCHRONIZATION_MASK)
// Max packet size is in bits 10..0
#define usb_ep_max_packet(ep) (le16toh((ep)->w_max_packet_size) & 0x07FF)
// For high speed interrupt and isochronous endpoints, additional transactions per microframe
// are in bits 12..11
#define usb_ep_add_mf_transactions(ep) ((le16toh((ep)->w_max_packet_size) >> 11) & 3)

__END_CDECLS

#ifdef __cplusplus
namespace usb {

class UsbDevice : public ddk::UsbProtocolClient {
 public:
  UsbDevice() {}
  UsbDevice(const usb_protocol_t* proto) : UsbProtocolClient(proto) {}

  UsbDevice(zx_device_t* parent) : UsbProtocolClient(parent) {}
  zx_status_t ClearFeature(uint8_t request_type, uint16_t feature, uint16_t index,
                           zx_time_t timeout) {
    usb_protocol_t proto;
    GetProto(&proto);
    return usb_clear_feature(&proto, request_type, feature, index, timeout);
  }
  zx_status_t GetDescriptor(uint8_t request_type, uint16_t type, uint16_t index, void* data,
                            size_t length, zx_time_t timeout, size_t* out_length) {
    usb_protocol_t proto;
    GetProto(&proto);
    return usb_get_descriptor(&proto, request_type, type, index, reinterpret_cast<uint8_t*>(data),
                              length, timeout, out_length);
  }
  zx_status_t GetStatus(uint8_t request_type, uint16_t index, void* data, size_t length,
                        zx_time_t timeout, size_t* out_length) {
    usb_protocol_t proto;
    GetProto(&proto);
    return usb_get_status(&proto, request_type, index, data, length, timeout, out_length);
  }
  zx_status_t SetFeature(int8_t request_type, uint16_t feature, uint16_t index, zx_time_t timeout) {
    usb_protocol_t proto;
    GetProto(&proto);
    return usb_set_feature(&proto, request_type, feature, index, timeout);
  }
};

// DescriptorList is used to iterate all of the USB descriptors of an Interface. It is created by
// calling GetDescriptorList on an Interface. The returned descriptor pointers are valid for the
// lifetime of the InterfaceList used to create the parent Interface. DescriptorList implements a
// standard C++ iterator interface that returns usb_descriptor_header_t*.
//
// Example Usage:
//   std::optional<InterfaceList> interfaces;
//   status = InterfaceList::Create(my_client, true, &interfaces);
//   if (status != ZX_OK) {
//     ...
//   }
//
//   // Find the first descriptor of type usb_my_device_specific_desc_t.
//   for (const auto& interface : *interfaces) {
//     for (auto& descriptor: interface.GetDescriptorList()) {
//       if (descriptor.b_descriptor_type == USB_DT_MY_DEVICE_SPECIFIC) {
//         return make_optional<usb_my_device_specific_desc_t*>(
//           reinterpret_cast<usb_my_device_specific_desc_t*>(&descriptor));
//       }
//     }
//   }
class DescriptorList {
 private:
  class iterator_impl;

 public:
  using iterator = iterator_impl;
  using const_iterator = iterator_impl;

  DescriptorList(const usb_desc_iter_t& iter, const usb_interface_descriptor_t* descriptor)
      : iter_(iter), descriptor_(descriptor) {}

  DescriptorList() = delete;

  const usb_interface_descriptor_t* descriptor() const { return descriptor_; }

  iterator begin() const;
  const_iterator cbegin() const;
  iterator end() const;
  const_iterator cend() const;

 private:
  class iterator_impl {
   public:
    friend class DescriptorList;

    iterator_impl(const usb_desc_iter_t& iter, const usb_descriptor_header_t* header)
        : iter_(iter), header_(header) {}

    bool operator==(const iterator_impl& other) const { return (other.header_ == header_); }
    bool operator!=(const iterator_impl& other) const { return !(*this == other); }

    iterator_impl operator++(int) {
      iterator_impl ret(*this);
      ++(*this);
      return ret;
    }

    iterator_impl& operator++() {
      ReadHeader(&iter_, &header_);
      return *this;
    }

    const usb_descriptor_header_t* header() const { return header_; }
    const usb_descriptor_header_t& operator*() const { return *header_; }
    const usb_descriptor_header_t* operator->() const { return header_; }

   private:
    // Using the given iter, read the next endpoint descriptor(s).
    static void ReadHeader(usb_desc_iter_t* iter, const usb_descriptor_header_t** out);

    usb_desc_iter_t iter_;
    const usb_descriptor_header_t* header_;
  };

  usb_desc_iter_t iter_;
  const usb_interface_descriptor_t* descriptor_;
};

// Endpoint is accessed by iterating on EndpointList. It contains pointers to an endpoint descriptor
// and its (optional) SuperSpeed companion descriptor (see usb3.2 ch9.6.7). The returned descriptor
// pointers are valid for the lifetime of the InterfaceList used to create the EndpointList (see
// EndpointList documentation below.)
class Endpoint {
 public:
  Endpoint(const usb_endpoint_descriptor_t* descriptor,
           std::optional<const usb_ss_ep_comp_descriptor_t*> ss_companion)
      : descriptor_(descriptor), ss_companion_(ss_companion) {}

  const usb_endpoint_descriptor_t* descriptor() const { return descriptor_; }

  std::optional<const usb_ss_ep_comp_descriptor_t*> ss_companion() const { return ss_companion_; }
  bool has_companion() const { return ss_companion_.has_value(); }

 private:
  const usb_endpoint_descriptor_t* descriptor_;
  std::optional<const usb_ss_ep_comp_descriptor_t*> ss_companion_;
};

// EndpointList is used to iterate all of the USB endpoint descriptors of an Interface. It is
// created by calling GetEndpointList on an Interface. The returned descriptor pointers are valid
// for the lifetime of the InterfaceList used to create the parent Interface. EndpointList
// implements a standard C++ iterator interface that returns Endpoint.
//
// Example Usage:
//   std::optional<InterfaceList> interfaces;
//   status = InterfaceList::Create(my_client, true, &interfaces);
//   if (status != ZX_OK) {
//     ...
//   }
//
//   // Find the first interrupt endpoint and copy it for use by the driver.
//   for (const auto& interface : *interfaces) {
//     for (auto& endpoint : interface.GetEndpointList()) {
//       if (usb_ep_direction(endpoint.descriptor()) == USB_ENDPOINT_IN &&
//           usb_ep_type(endpoint.descriptor()) == USB_ENDPOINT_INTERRUPT) {
//         return std::make_optional<usb_endpoint_descriptor_t>(*endpoint.descriptor());
//       }
//     }
//   }
class EndpointList {
 private:
  class iterator_impl;

 public:
  using iterator = iterator_impl;
  using const_iterator = iterator_impl;

  EndpointList(const usb_desc_iter_t& iter, const usb_interface_descriptor_t* descriptor)
      : iter_(iter), descriptor_(descriptor) {}

  EndpointList() = delete;

  const usb_interface_descriptor_t* descriptor() const { return descriptor_; }

  iterator begin() const;
  const_iterator cbegin() const;
  iterator end() const;
  const_iterator cend() const;

 private:
  class iterator_impl {
   public:
    friend class EndpointList;

    bool operator==(const iterator_impl& other) const {
      return endpoint_.descriptor() == other.endpoint_.descriptor();
    }
    bool operator!=(const iterator_impl& other) const { return !(*this == other); }

    iterator_impl operator++(int) {
      iterator_impl ret(*this);
      ++(*this);
      return ret;
    }

    iterator_impl& operator++() {
      endpoint_ = ReadEp(&iter_);
      return *this;
    }

    const Endpoint& operator*() const { return endpoint_; }
    const Endpoint* operator->() const { return &endpoint_; }

   private:
    iterator_impl(const usb_desc_iter_t& iter, Endpoint endpoint)
        : iter_(iter), endpoint_(endpoint) {}

    // Using the given iter, read the next endpoint descriptor(s).
    static Endpoint ReadEp(usb_desc_iter_t* iter);

    usb_desc_iter_t iter_;
    Endpoint endpoint_;
  };

  usb_desc_iter_t iter_;
  const usb_interface_descriptor_t* descriptor_;
};

// Interface is accessed by iterating on InterfaceList. It contains a pointer to an interface
// descriptor. The returned descriptor pointer is valid for the lifetime of the InterfaceList used
// to create the Interface.
class Interface {
 public:
  DescriptorList GetDescriptorList() const;
  EndpointList GetEndpointList() const;
  const usb_interface_descriptor_t* descriptor() const { return descriptor_; }

  friend class InterfaceList;

 private:
  Interface(const usb_desc_iter_t& iter, const usb_interface_descriptor_t* descriptor)
      : descriptor_(descriptor), iter_(iter) {}

  // Advances iter_ to the next usb_interface_descriptor_t.
  void Next(bool skip_alt);

  const usb_interface_descriptor_t* descriptor_;
  usb_desc_iter_t iter_;
};

// An InterfaceList can be used for enumerating USB interfaces. It implements a standard C++
// iterator interface that returns Interface. All descriptors accessed by child classes are valid
// only for the lifetime of this InterfaceList object.
//
// The InterfaceList will skip any alternate interfaces if skip_alt is true (see usb2.0 ch9.6.5).
class InterfaceList {
 private:
  class iterator_impl;

 public:
  using iterator = iterator_impl;
  using const_iterator = iterator_impl;

  InterfaceList() = delete;

  InterfaceList(const usb_desc_iter_t& iter, bool skip_alt) : iter_(iter), skip_alt_(skip_alt) {}

  InterfaceList(InterfaceList&&) = delete;
  InterfaceList& operator=(InterfaceList&&) = delete;

  ~InterfaceList() {
    if (iter_.desc) {
      usb_desc_iter_release(&iter_);
    }
  }

  static zx_status_t Create(const ddk::UsbProtocolClient& client, bool skip_alt,
                            std::optional<InterfaceList>* out);

  size_t size() {
    return reinterpret_cast<size_t>(iter_.desc_end) - reinterpret_cast<size_t>(iter_.desc);
  }

  iterator begin() const;
  const_iterator cbegin() const;
  iterator end() const;
  const_iterator cend() const;

 private:
  class iterator_impl {
   public:
    iterator_impl(const usb_desc_iter_t& iter, bool skip_alt,
                  const usb_interface_descriptor_t* descriptor)
        : skip_alt_(skip_alt), interface_(iter, descriptor) {}

    bool operator==(const iterator_impl& other) const {
      return interface_.descriptor_ == other.interface_.descriptor_;
    }
    bool operator!=(const iterator_impl& other) const { return !(*this == other); }

    iterator_impl operator++(int) {
      iterator_impl ret(*this);
      ++(*this);
      return ret;
    }

    iterator_impl& operator++() {
      interface_.Next(skip_alt_);
      return *this;
    }

    const Interface* get() const { return &interface_; }
    const Interface& operator*() const { return interface_; }
    const Interface* operator->() const { return &interface_; }

   private:
    const bool skip_alt_;
    Interface interface_;
  };

  usb_desc_iter_t iter_{};
  bool skip_alt_;
};

}  // namespace usb
#endif

#endif  // SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_USB_H_
