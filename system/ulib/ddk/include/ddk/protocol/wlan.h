// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

typedef struct wlan_channel {
    uint16_t channel_num;
    // etc
} wlan_channel_t;

typedef struct wlanmac_ifc {
    // Report the status of the wlanmac device
    void (*status)(void* cookie, uint32_t status);

    // Queue received data for processing by the wlan mac driver
    void (*recv)(void* cookie, void* data, size_t length, uint32_t flags);
} wlanmac_ifc_t;


typedef struct wlanmac_protocol {
    // Obtain information about the device and supported features
    // Safe to call at any time.
    // TODO: create wlanmac_info_t for wlan-specific info and copy the relevant
    // ethernet fields into ethmac_info_t before passing up the stack
    mx_status_t (*query)(mx_device_t* dev, uint32_t options, ethmac_info_t* info);

    // Start wlanmac running with ifc_virt
    // Callbacks on ifc may be invoked from now until stop() is called
    mx_status_t (*start)(mx_device_t* dev, wlanmac_ifc_t* ifc, void* cookie);

    // Shut down a running wlanmac
    // Safe to call if the wlanmac is already stopped.
    void (*stop)(mx_device_t* dev);

    // Queue the data for transmit
    void (*tx)(mx_device_t* dev, uint32_t options, void* data, size_t length);

    // Set the radio channel
    mx_status_t (*set_channel)(mx_device_t* dev, uint32_t options, wlan_channel_t* chan);
} wlanmac_protocol_t;


__END_CDECLS;
