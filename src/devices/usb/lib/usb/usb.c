// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>
#include <zircon/compiler.h>

#include <usb/usb.h>

// initializes a usb_desc_iter_t for iterating on descriptors past the
// interface's existing descriptors.
static zx_status_t usb_desc_iter_additional_init(usb_composite_protocol_t* comp,
                                                 usb_desc_iter_t* iter) {
  memset(iter, 0, sizeof(*iter));

  size_t length = usb_composite_get_additional_descriptor_length(comp);
  uint8_t* descriptors = malloc(length);
  if (!descriptors) {
    return ZX_ERR_NO_MEMORY;
  }
  size_t actual;
  zx_status_t status =
      usb_composite_get_additional_descriptor_list(comp, descriptors, length, &actual);
  if (status != ZX_OK) {
    return status;
  }

  iter->desc = descriptors;
  iter->desc_end = descriptors + length;
  iter->current = descriptors;
  return ZX_OK;
}

// helper function for claiming additional interfaces that satisfy the want_interface predicate,
// want_interface will be passed the supplied arg
__EXPORT zx_status_t usb_claim_additional_interfaces(
    usb_composite_protocol_t* comp, bool (*want_interface)(usb_interface_descriptor_t*, void*),
    void* arg) {
  usb_desc_iter_t iter;
  zx_status_t status = usb_desc_iter_additional_init(comp, &iter);
  if (status != ZX_OK) {
    return status;
  }

  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  while (intf != NULL && want_interface(intf, arg)) {
    // We need to find the start of the next interface to calculate the
    // total length of the current one.
    usb_interface_descriptor_t* next = usb_desc_iter_next_interface(&iter, true);
    // If we're currently on the last interface, next will be NULL.
    void* intf_end = next ? next : (void*)iter.desc_end;
    size_t length = intf_end - (void*)intf;

    ZX_ASSERT(length < UINT32_MAX);
    status = usb_composite_claim_interface(comp, intf, (uint32_t)length);
    if (status != ZX_OK) {
      break;
    }
    intf = next;
  }
  usb_desc_iter_release(&iter);
  return status;
}

// initializes a usb_desc_iter_t
__EXPORT zx_status_t usb_desc_iter_init(usb_protocol_t* usb, usb_desc_iter_t* iter) {
  memset(iter, 0, sizeof(*iter));

  size_t length = usb_get_descriptors_length(usb);
  void* descriptors = malloc(length);
  if (!descriptors) {
    return ZX_ERR_NO_MEMORY;
  }
  size_t actual;
  usb_get_descriptors(usb, descriptors, length, &actual);

  iter->desc = descriptors;
  iter->desc_end = descriptors + length;
  iter->current = descriptors;
  return ZX_OK;
}

// clones a usb_desc_iter_t
zx_status_t usb_desc_iter_clone(const usb_desc_iter_t* src, usb_desc_iter_t* dest) {
  size_t length = (size_t)(src->desc_end) - (size_t)(src->desc);
  size_t offset = (size_t)(src->current) - (size_t)(src->desc);
  void* descriptors = malloc(length);
  if (!descriptors) {
    return ZX_ERR_NO_MEMORY;
  }
  memcpy(descriptors, src->desc, length);
  dest->desc = descriptors;
  dest->current = ((unsigned char*)descriptors) + offset;
  dest->desc_end = ((unsigned char*)descriptors) + length;
  return ZX_OK;
}

bool safe_add(uint8_t* a, size_t b, uint8_t** sum) {
  uint8_t* test_sum = a + b;
  if (test_sum < a) {
    return false;
  }
  *sum = test_sum;
  return true;
}

// releases resources in a usb_desc_iter_t
__EXPORT void usb_desc_iter_release(usb_desc_iter_t* iter) {
  free(iter->desc);
  iter->desc = NULL;
}

// resets iterator to the beginning
__EXPORT void usb_desc_iter_reset(usb_desc_iter_t* iter) { iter->current = iter->desc; }

// increase the iterator to the next descriptor. If the current descriptor is not a valid descriptor
// header structure, returns false, otherwise, returns true. The iterator would not be increased
// if false is returned and user is expected to handle the error case and end the descriptor
// parsing.
__EXPORT bool usb_desc_iter_advance(usb_desc_iter_t* iter) {
  usb_descriptor_header_t* header = usb_desc_iter_peek(iter);
  if (!header)
    return false;
  iter->current += header->b_length;
  return true;
}

// returns the descriptor header structure currently pointed by the iterator. If the current
// iterator does not point to a valid descriptor header structure, NULL would be returned and user
// is expected to handle the error case and end the descriptor parsing.
__EXPORT usb_descriptor_header_t* usb_desc_iter_peek(usb_desc_iter_t* iter) {
  uint8_t* end;
  if (!safe_add(iter->current, sizeof(usb_descriptor_header_t), &end)) {
    return NULL;
  }
  if (end > iter->desc_end) {
    return NULL;
  }
  usb_descriptor_header_t* header = (usb_descriptor_header_t*)iter->current;
  if (!safe_add(iter->current, header->b_length, &end)) {
    return NULL;
  }
  if (end > iter->desc_end) {
    return NULL;
  }
  if (header->b_length == 0) {
    // An descriptor must not have 0 length, otherwise, it might cause infinite loop.
    return NULL;
  }
  return header;
}

// returns the expected structure with structure size currently pointed by the iterator. If the
// length of descriptor buffer current pointed by the iterator is not enough to hold the structure,
// NULL would be returned, user is expected to handle the error case.
__EXPORT void* usb_desc_iter_get_structure(usb_desc_iter_t* iter, size_t structure_size) {
  uint8_t* start = (uint8_t*)iter->current;
  uint8_t* end = 0;
  if (!safe_add(start, structure_size, &end)) {
    return NULL;
  }
  if (end > iter->desc_end) {
    return NULL;
  }
  return (void*)start;
}

// returns the next interface descriptor, optionally skipping alternate interfaces
__EXPORT usb_interface_descriptor_t* usb_desc_iter_next_interface(usb_desc_iter_t* iter,
                                                                  bool skip_alt) {
  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(iter)) != NULL) {
    if (header->b_descriptor_type == USB_DT_INTERFACE) {
      usb_interface_descriptor_t* desc =
          usb_desc_iter_get_structure(iter, sizeof(usb_interface_descriptor_t));
      if (desc == NULL) {
        return NULL;
      }
      if (!skip_alt || desc->b_alternate_setting == 0) {
        usb_desc_iter_advance(iter);
        return desc;
      }
    }
    usb_desc_iter_advance(iter);
  }
  // not found
  return NULL;
}

// returns the next endpoint descriptor within the current interface
__EXPORT usb_endpoint_descriptor_t* usb_desc_iter_next_endpoint(usb_desc_iter_t* iter) {
  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(iter)) != NULL) {
    if (header->b_descriptor_type == USB_DT_INTERFACE) {
      // we are at end of previous interface
      return NULL;
    }
    if (header->b_descriptor_type == USB_DT_ENDPOINT) {
      usb_endpoint_descriptor_t* desc =
          usb_desc_iter_get_structure(iter, sizeof(usb_endpoint_descriptor_t));
      if (desc == NULL) {
        return NULL;
      }
      usb_desc_iter_advance(iter);
      return desc;
    }
    usb_desc_iter_advance(iter);
  }
  // not found
  return NULL;
}

// returns the next ss-companion descriptor within the current interface.
// drivers may use usb_desc_iter_peek() to determine if an endpoint or ss_companion descriptor is
// expected.
__EXPORT usb_ss_ep_comp_descriptor_t* usb_desc_iter_next_ss_ep_comp(usb_desc_iter_t* iter) {
  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(iter)) != NULL) {
    uint8_t desc_type = header->b_descriptor_type;
    if (desc_type == USB_DT_ENDPOINT || desc_type == USB_DT_INTERFACE) {
      // we are either at next endpoint or end of previous interface
      return NULL;
    }
    if (header->b_descriptor_type == USB_DT_SS_EP_COMPANION) {
      usb_ss_ep_comp_descriptor_t* desc =
          usb_desc_iter_get_structure(iter, sizeof(usb_ss_ep_comp_descriptor_t));
      if (desc == NULL) {
        return NULL;
      }
      usb_desc_iter_advance(iter);
      return desc;
    }
    usb_desc_iter_advance(iter);
  }
  // not found
  return NULL;
}
