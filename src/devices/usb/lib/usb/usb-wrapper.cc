// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <memory>

#include <usb/usb.h>

namespace usb {

zx_status_t InterfaceList::Create(const ddk::UsbProtocolClient& client, bool skip_alt,
                                  std::optional<InterfaceList>* out) {
  usb_protocol_t proto;
  client.GetProto(&proto);

  usb_desc_iter_t iter;
  auto status = usb_desc_iter_init(&proto, &iter);
  if (status != ZX_OK) {
    return status;
  }

  out->emplace(iter, skip_alt);
  return ZX_OK;
}

InterfaceList::iterator InterfaceList::begin() const {
  if (!iter_.desc) {
    return end();
  }
  usb_desc_iter_t iter = iter_;
  const usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, skip_alt_);
  return iterator(iter, skip_alt_, intf);
}

InterfaceList::const_iterator InterfaceList::cbegin() const {
  return static_cast<InterfaceList::const_iterator>(begin());
}

InterfaceList::iterator InterfaceList::end() const {
  usb_desc_iter_t init = {};
  return iterator(init, skip_alt_, nullptr);
}

InterfaceList::const_iterator InterfaceList::cend() const {
  return static_cast<InterfaceList::const_iterator>(end());
}

void Interface::Next(bool skip_alt) {
  descriptor_ = usb_desc_iter_next_interface(&iter_, skip_alt);
}

EndpointList::iterator EndpointList::begin() const {
  if (!iter_.desc) {
    return end();
  }
  usb_desc_iter_t iter = iter_;
  usb_iter_endpoint_descriptor_t endpoint{};
  EndpointList::iterator::ReadEp(&iter, &endpoint);

  return iterator(iter, endpoint);
}

EndpointList::const_iterator EndpointList::cbegin() const {
  return static_cast<EndpointList::const_iterator>(begin());
}

EndpointList::iterator EndpointList::end() const {
  usb_desc_iter_t init = {};
  return iterator(init, usb_iter_endpoint_descriptor_t{});
}

EndpointList::const_iterator EndpointList::cend() const {
  return static_cast<EndpointList::const_iterator>(end());
}

void EndpointList::iterator::ReadEp(usb_desc_iter_t* iter, usb_iter_endpoint_descriptor_t* out) {
  const usb_endpoint_descriptor_t* ptr = usb_desc_iter_next_endpoint(iter);
  if (ptr) {
    out->descriptor = *ptr;
  }

  // A SuperSpeed companion descriptor may optionally follow.
  const usb_descriptor_header_t* header = usb_desc_iter_peek(iter);
  if (header && header->b_descriptor_type == USB_DT_SS_EP_COMPANION) {
    out->ss_companion = *usb_desc_iter_next_ss_ep_comp(iter);
    out->has_companion = true;
  }
}

EndpointList Interface::GetEndpointList() const { return EndpointList(iter_, descriptor_); }

DescriptorList Interface::GetDescriptorList() const { return DescriptorList(iter_, descriptor_); }

DescriptorList::iterator DescriptorList::begin() const {
  if (!iter_.desc) {
    return end();
  }
  usb_desc_iter_t iter = iter_;
  const usb_descriptor_header_t* header;
  DescriptorList::iterator::ReadHeader(&iter, &header);
  return iterator(iter, header);
}

DescriptorList::const_iterator DescriptorList::cbegin() const {
  return static_cast<DescriptorList::const_iterator>(begin());
}

DescriptorList::iterator DescriptorList::end() const {
  usb_desc_iter_t init = {};
  return iterator(init, nullptr);
}

DescriptorList::const_iterator DescriptorList::cend() const {
  return static_cast<DescriptorList::const_iterator>(end());
}

void DescriptorList::iterator::ReadHeader(usb_desc_iter_t* iter,
                                          const usb_descriptor_header_t** out) {
  const usb_descriptor_header_t* ptr = usb_desc_iter_peek(iter);
  usb_desc_iter_advance(iter);
  if (ptr && ptr->b_descriptor_type != USB_DT_INTERFACE) {
    *out = ptr;
  } else {
    *out = nullptr;
  }
}

}  // namespace usb
