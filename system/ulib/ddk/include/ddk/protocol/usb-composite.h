// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/hw/usb.h>

__BEGIN_CDECLS;

typedef struct {
    zx_status_t (*get_additional_descriptor_list)(void* ctx, void** out_descriptors,
                                                  size_t* out_length);
    zx_status_t (*claim_interface)(void* ctx, usb_interface_descriptor_t* intf, size_t length);
} usb_composite_protocol_ops_t;

typedef struct {
    usb_composite_protocol_ops_t* ops;
    void* ctx;
} usb_composite_protocol_t;

// returns the USB descriptors following the interface's existing descriptors
// the returned value is de-allocated with free()
static inline zx_status_t usb_composite_get_additional_descriptor_list(
                                                        const usb_composite_protocol_t* comp,
                                                        void** out_descriptors,
                                                        size_t* out_length) {
    return comp->ops->get_additional_descriptor_list(comp->ctx, out_descriptors, out_length);
}

// marks the interface as claimed and appends the interface descriptor to the
// interface's existing descriptors.
static inline zx_status_t usb_composite_claim_interface(const usb_composite_protocol_t* comp,
                                                         usb_interface_descriptor_t* intf,
                                                         size_t length) {
    return comp->ops->claim_interface(comp->ctx, intf, length);
}

__END_CDECLS;
