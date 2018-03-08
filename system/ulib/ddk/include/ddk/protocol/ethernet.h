// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/device/ethernet.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

#define ETH_MAC_SIZE 6

// The ethermac interface supports both synchronous and asynchronous transmissions using the
// proto->queue_tx() and ifc->complete_tx() methods.
//
// Receive operations are supported with the ifc->recv() interface.
// TODO: implement netbuf-based receive operations by implementing proto->queue_rx() and
// ifc->complete_rx()
//
// The FEATURE_WLAN flag indicates a device that supports wlan operations.
//
// The FEATURE_SYNTH flag indicates a device that is not backed by hardware.
//
// The FEATURE_DMA flag indicates that the device can copy the buffer data using DMA and will ensure
// that physical addresses are provided in netbufs.

#define ETHMAC_FEATURE_WLAN     (1u)
#define ETHMAC_FEATURE_SYNTH    (2u)
#define ETHMAC_FEATURE_DMA      (4u)

typedef struct ethmac_info {
    uint32_t features;
    uint32_t mtu;
    uint8_t mac[ETH_MAC_SIZE];
    uint8_t reserved0[2];
    uint32_t reserved1[4];
} ethmac_info_t;

typedef struct ethmac_netbuf {
    // Provided by the generic ethernet driver
    void* data;
    zx_paddr_t phys;  // Only used if ETHMAC_FEATURE_DMA is available
    uint16_t len;
    uint16_t reserved;
    uint32_t flags;

    // Shared between the generic ethernet and ethmac drivers
    list_node_t node;

    // For use by the ethmac driver
    union {
        uint64_t val;
        void* ptr;
    };
} ethmac_netbuf_t;

typedef struct ethmac_ifc_virt {
    void (*status)(void* cookie, uint32_t status);

    void (*recv)(void* cookie, void* data, size_t length, uint32_t flags);

    // complete_tx() is called to return ownership of a netbuf to the generic ethernet driver.
    void (*complete_tx)(void* cookie, ethmac_netbuf_t* netbuf, zx_status_t status);
} ethmac_ifc_t;

// Indicates that additional data is available to be sent after this call finishes. Allows a ethmac
// driver to batch tx to hardware if possible.
#define ETHMAC_TX_OPT_MORE (1u)

// SETPARAM_ values identify the parameter to set. Each call to set_param()
// takes an int32_t |value| and void* |data| which have meaning specific to
// the parameter being set.

// |value| is bool. |data| is unused.
#define ETHMAC_SETPARAM_PROMISC (1u)

// |value| is bool. |data| is unused.
#define ETHMAC_SETPARAM_MULTICAST_PROMISC (2u)

#define ETHMAC_MULTICAST_FILTER_OVERFLOW -1

// |value| is number of addresses, or ETHMAC_MULTICAST_FILTER_OVERFLOW for "too many to count."
// |data| is |value|*6 bytes of MAC addresses. Caller retains ownership.
// If |value| is _OVERFLOW, |data| is ignored.
#define ETHMAC_SETPARAM_MULTICAST_FILTER (3u)

#define ETHMAC_SETPARAM_DUMP_REGS (4u)

// The ethernet midlayer will never call ethermac_protocol
// methods from multiple threads simultaneously, but it
// can call send() methods at the same time as non-send
// methods.
typedef struct ethmac_protocol_ops {
    // Obtain information about the ethermac device and supported features
    // Safe to call at any time.
    zx_status_t (*query)(void* ctx, uint32_t options, ethmac_info_t* info);

    // Shut down a running ethermac
    // Safe to call if the ethermac is already stopped.
    void (*stop)(void* ctx);

    // Start ethermac running with ifc_virt
    // Callbacks on ifc may be invoked from now until stop() is called
    zx_status_t (*start)(void* ctx, ethmac_ifc_t* ifc, void* cookie);

    // Request transmission of the packet in netbuf. Return status indicates disposition:
    //   ZX_ERR_SHOULD_WAIT: Packet is being transmitted
    //   ZX_OK: Packet has been transmitted
    //   Other: Packet could not be transmitted
    //
    // In the SHOULD_WAIT case the driver takes ownership of the netbuf and must call complete_tx()
    // to return it once the transmission is complete. complete_tx() MUST NOT be called from within
    // the queue_tx() implementation.
    //
    // queue_tx() may be called at any time after start() is called including from multiple threads
    // simultaneously.
    zx_status_t (*queue_tx)(void* ctx, uint32_t options, ethmac_netbuf_t* netbuf);

    // Request a settings change for the driver. Return status indicates disposition:
    //   ZX_OK: Request has been handled.
    //   ZX_ERR_NOT_SUPPORTED: Driver does not support this setting.
    //   Other: Error trying to support this request.
    //
    // |value| and |data| usage are defined for each |param|; see comments above.
    //
    // set_param() may be called at any time after start() is called including from multiple threads
    // simultaneously.
    zx_status_t (*set_param)(void* ctx, uint32_t param, int32_t value, void* data);

    // Get the BTI handle (needed to pin DMA memory) for this device.
    // This method is only valid on devices that advertise ETHMAC_FEATURE_DMA
    // The caller does *not* take ownership of the BTI handle and must never close
    // the handle.
    zx_handle_t (*get_bti)(void* ctx);
} ethmac_protocol_ops_t;

typedef struct ethmac_protocol {
    ethmac_protocol_ops_t* ops;
    void* ctx;
} ethmac_protocol_t;

__END_CDECLS;
