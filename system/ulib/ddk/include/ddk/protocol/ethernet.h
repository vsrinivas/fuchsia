// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/device/ethernet.h>

__BEGIN_CDECLS;

#define ETH_MAC_SIZE 6

// The ethermac interface supports a simple copying interface
// via proto->send() and ifc->recv() and a zero-copy interface
// via proto->queue_?X() and ifc->complete_?x()
//
// The FEATURE_?X_QUEUE flags indicate the use of the zero-copy
// interface (which is selectable independently for transmit and
// receive)
//
// TODO: Implement zero-copy interface in the ethernet common
// middle layer driver.  Currently ethermac drivers that request
// these will not be loaded.
//
// The FEATURE_WLAN flag indicates a device that supports wlan operations.
//
// The FEATURE_SYNTH flag indicates a device that is not backed by hardware.

#define ETHMAC_FEATURE_RX_QUEUE (1u)
#define ETHMAC_FEATURE_TX_QUEUE (2u)
#define ETHMAC_FEATURE_WLAN     (4u)
#define ETHMAC_FEATURE_SYNTH    (8u)

typedef struct ethmac_info {
    uint32_t features;
    uint32_t mtu;
    uint8_t mac[ETH_MAC_SIZE];
    uint8_t reserved0[2];
    uint32_t reserved1[4];
} ethmac_info_t;

typedef struct ethmac_ifc_virt {
    void (*status)(void* cookie, uint32_t status);

    // recv() is invoked when FEATURE_RX_QUEUE is not present
    void (*recv)(void* cookie, void* data, size_t length, uint32_t flags);

    // complete_?x() is invoked when FEATURE_?X_QUEUE is present
    void (*complete_rx)(void* cookie, uint32_t length, uint32_t flags);
    void (*complete_tx)(void* cookie, uint32_t count);
} ethmac_ifc_t;

// Indicates that additional data is available to be sent after this call finishes. Allows a ethmac
// driver to batch tx to hardware if possible.
#define ETHMAC_TX_OPT_MORE (1u)

// The ethernet midlayer will never call ethermac_protocol
// methods from multiple threads simultaneously, but it
// can call send() methods at the same time as non-send
// methods.
typedef struct ethmac_protocol_ops {
    // Obtain information about the ethermac device and supported features
    // Safe to call at any time.
    mx_status_t (*query)(void* ctx, uint32_t options, ethmac_info_t* info);

    // Shut down a running ethermac
    // Safe to call if the ethermac is already stopped.
    void (*stop)(void* ctx);

    // Start ethermac running with ifc_virt
    // Callbacks on ifc may be invoked from now until stop() is called
    mx_status_t (*start)(void* ctx, ethmac_ifc_t* ifc, void* cookie);

    // send() is valid if FEATURE_TX_QUEUE is not present, otherwise it is no-op
    // This may be called at any time, and can be called from multiple
    // threads simultaneously.
    void (*send)(void* ctx, uint32_t options, void* data, size_t length);

    // queue_?x() is valid if FEATURE_?X_QUEUE is present, otherwise they are no-op
    void (*queue_tx)(void* ctx, uint32_t options,
                     uintptr_t pa0, uintptr_t pa1, size_t length);
    void (*queue_rx)(void* ctx, uint32_t options,
                     uintptr_t pa0, uintptr_t pa1, size_t length);
} ethmac_protocol_ops_t;

typedef struct ethmac_protocol {
    ethmac_protocol_ops_t* ops;
    void* ctx;
} ethmac_protocol_t;

__END_CDECLS;
