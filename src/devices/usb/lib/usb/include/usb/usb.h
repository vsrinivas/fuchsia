// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_USB_H_
#define SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_USB_H_

#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/composite/c/banjo.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#ifdef __cplusplus
#include <fuchsia/hardware/usb/cpp/banjo.h>

#include <optional>
#endif

__BEGIN_CDECLS

// maximum number of endpoints per device
#define USB_MAX_EPS 32

/* Request Types */
#define USB_DIR_OUT (0 << 7)
#define USB_DIR_IN (1 << 7)
#define USB_DIR_MASK (1 << 7)
#define USB_TYPE_STANDARD (0 << 5)
#define USB_TYPE_CLASS (1 << 5)
#define USB_TYPE_VENDOR (2 << 5)
#define USB_TYPE_MASK (3 << 5)
#define USB_RECIP_DEVICE (0 << 0)
#define USB_RECIP_INTERFACE (1 << 0)
#define USB_RECIP_ENDPOINT (2 << 0)
#define USB_RECIP_OTHER (3 << 0)
#define USB_RECIP_MASK (0x1f << 0)

/* 1.0 Request Values */
#define USB_REQ_GET_STATUS 0x00
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_REQ_SET_FEATURE 0x03
#define USB_REQ_SET_ADDRESS 0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_DESCRIPTOR 0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE 0x0A
#define USB_REQ_SET_INTERFACE 0x0B
#define USB_REQ_SYNCH_FRAME 0x0C

/* USB device/interface classes */
#define USB_CLASS_AUDIO 0x01
#define USB_CLASS_COMM 0x02
#define USB_CLASS_HID 0x03
#define USB_CLASS_PHYSICAL 0x05
#define USB_CLASS_IMAGING 0x06
#define USB_CLASS_PRINTER 0x07
#define USB_CLASS_MSC 0x08
#define USB_CLASS_HUB 0x09
#define USB_CLASS_CDC 0x0a
#define USB_CLASS_CCID 0x0b
#define USB_CLASS_SECURITY 0x0d
#define USB_CLASS_VIDEO 0x0e
#define USB_CLASS_HEALTHCARE 0x0f
#define USB_CLASS_DIAGNOSTIC 0xdc
#define USB_CLASS_WIRELESS 0xe0
#define USB_CLASS_MISC 0xef
#define USB_CLASS_APPLICATION_SPECIFIC 0xfe
#define USB_CLASS_VENDOR 0xFf

#define USB_SUBCLASS_COMM_ACM 0x02

#define USB_SUBCLASS_WIRELESS_MISC 0x01
#define USB_PROTOCOL_WIRELESS_MISC_RNDIS 0x03

#define USB_SUBCLASS_MSC_RNDIS 0x04
#define USB_PROTOCOL_MSC_RNDIS_ETHERNET 0x01

#define USB_SUBCLASS_MSC_SCSI 0x06
#define USB_PROTOCOL_MSC_BULK_ONLY 0x50

#define USB_SUBCLASS_DFU 0x01
#define USB_PROTOCOL_DFU 0x02

#define USB_SUBCLASS_ADB 0x42
#define USB_PROTOCOL_ADB 0x01

#define USB_SUBCLASS_VENDOR 0xFF
#define USB_PROTOCOL_TEST_FTDI 0x01
#define USB_PROTOCOL_TEST_HID_ONE_ENDPOINT 0x02
#define USB_PROTOCOL_TEST_HID_TWO_ENDPOINT 0x03

/* Descriptor Types */
#define USB_DT_DEVICE 0x01
#define USB_DT_CONFIG 0x02
#define USB_DT_STRING 0x03
#define USB_DT_INTERFACE 0x04
#define USB_DT_ENDPOINT 0x05
#define USB_DT_DEVICE_QUALIFIER 0x06
#define USB_DT_OTHER_SPEED_CONFIG 0x07
#define USB_DT_INTERFACE_POWER 0x08
#define USB_DT_INTERFACE_ASSOCIATION 0x0b
#define USB_DT_HID 0x21
#define USB_DT_HIDREPORT 0x22
#define USB_DT_HIDPHYSICAL 0x23
#define USB_DT_CS_INTERFACE 0x24
#define USB_DT_CS_ENDPOINT 0x25
#define USB_DT_SS_EP_COMPANION 0x30
#define USB_DT_SS_ISOCH_EP_COMPANION 0x31

/* USB device feature selectors */
#define USB_DEVICE_SELF_POWERED 0x00
#define USB_DEVICE_REMOTE_WAKEUP 0x01
#define USB_DEVICE_TEST_MODE 0x02

/* Configuration attributes (bm_attributes) */
#define USB_CONFIGURATION_REMOTE_WAKEUP 0x20
#define USB_CONFIGURATION_SELF_POWERED 0x40
#define USB_CONFIGURATION_RESERVED_7 0x80  // This bit must be set

/* Endpoint direction (bEndpointAddress) */
#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_OUT 0x00
#define USB_ENDPOINT_DIR_MASK 0x80
#define USB_ENDPOINT_NUM_MASK 0x1F

/* Endpoint types (bm_attributes) */
#define USB_ENDPOINT_CONTROL 0x00
#define USB_ENDPOINT_ISOCHRONOUS 0x01
#define USB_ENDPOINT_BULK 0x02
#define USB_ENDPOINT_INTERRUPT 0x03
#define USB_ENDPOINT_TYPE_MASK 0x03

/* Endpoint synchronization type (bm_attributes) */
#define USB_ENDPOINT_NO_SYNCHRONIZATION 0x00
#define USB_ENDPOINT_ASYNCHRONOUS 0x04
#define USB_ENDPOINT_ADAPTIVE 0x08
#define USB_ENDPOINT_SYNCHRONOUS 0x0C
#define USB_ENDPOINT_SYNCHRONIZATION_MASK 0x0C

/* Endpoint usage type (bm_attributes) */
#define USB_ENDPOINT_DATA 0x00
#define USB_ENDPOINT_FEEDBACK 0x10
#define USB_ENDPOINT_IMPLICIT_FEEDBACK 0x20
#define USB_ENDPOINT_USAGE_MASK 0x30

#define USB_ENDPOINT_HALT 0x00

// TODO(fxbug.dev/111397) : Some of these structs are duplicates of usb banjo. Remove and
// consolidate them.
/* general USB defines */
typedef struct {
  uint8_t bm_request_type;
  uint8_t b_request;
  uint16_t w_value;
  uint16_t w_index;
  uint16_t w_length;
} __attribute__((packed)) usb_setup_info_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;
} __attribute__((packed)) usb_descriptor_header_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_DEVICE
  uint16_t bcd_usb;
  uint8_t b_device_class;
  uint8_t b_device_sub_class;
  uint8_t b_device_protocol;
  uint8_t b_max_packet_size0;
  uint16_t id_vendor;
  uint16_t id_product;
  uint16_t bcd_device;
  uint8_t i_manufacturer;
  uint8_t i_product;
  uint8_t i_serial_number;
  uint8_t b_num_configurations;
} __attribute__((packed)) usb_device_descriptor_info_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_CONFIG
  uint16_t w_total_length;
  uint8_t b_num_interfaces;
  uint8_t b_configuration_value;
  uint8_t i_configuration;
  uint8_t bm_attributes;
  uint8_t b_max_power;
} __attribute__((packed)) usb_configuration_descriptor_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_STRING
  uint8_t b_string[];
} __attribute__((packed)) usb_string_descriptor_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_INTERFACE
  uint8_t b_interface_number;
  uint8_t b_alternate_setting;
  uint8_t b_num_endpoints;
  uint8_t b_interface_class;
  uint8_t b_interface_sub_class;
  uint8_t b_interface_protocol;
  uint8_t i_interface;
} __attribute__((packed)) usb_interface_info_descriptor_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_ENDPOINT
  uint8_t b_endpoint_address;
  uint8_t bm_attributes;
  uint16_t w_max_packet_size;
  uint8_t b_interval;
} __attribute__((packed)) usb_endpoint_info_descriptor_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_DEVICE_QUALIFIER
  uint16_t bcd_usb;
  uint8_t b_device_class;
  uint8_t b_device_sub_class;
  uint8_t b_device_protocol;
  uint8_t b_max_packet_size0;
  uint8_t b_num_configurations;
  uint8_t b_reserved;
} __attribute__((packed)) usb_device_qualifier_descriptor_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_SS_EP_COMPANION
  uint8_t b_max_burst;
  uint8_t bm_attributes;
  uint16_t w_bytes_per_interval;
} __attribute__((packed)) usb_ss_ep_comp_descriptor_info_t;
#define usb_ss_ep_comp_isoc_mult(ep) ((ep)->bm_attributes & 0x3)
#define usb_ss_ep_comp_isoc_comp(ep) (!!((ep)->bm_attributes & 0x80))

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_SS_ISOCH_EP_COMPANION
  uint16_t w_reserved;
  uint32_t dw_bytes_per_interval;
} __attribute__((packed)) usb_ss_isoch_ep_comp_descriptor_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_INTERFACE_ASSOCIATION
  uint8_t b_first_interface;
  uint8_t b_interface_count;
  uint8_t b_function_class;
  uint8_t b_function_sub_class;
  uint8_t b_function_protocol;
  uint8_t i_function;
} __attribute__((packed)) usb_interface_assoc_descriptor_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_CS_INTERFACE
  uint8_t b_descriptor_sub_type;
} __attribute__((packed)) usb_cs_interface_descriptor_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_STRING
  uint16_t w_lang_ids[127];
} __attribute__((packed)) usb_langid_desc_t;

typedef struct {
  uint8_t b_length;
  uint8_t b_descriptor_type;  // USB_DT_STRING
  uint16_t code_points[127];
} __attribute__((packed)) usb_string_desc_t;

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
