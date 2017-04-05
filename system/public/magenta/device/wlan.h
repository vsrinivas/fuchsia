// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

__BEGIN_CDECLS

#define IOCTL_WLAN_GET_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_WLAN, 0)

// ssize_t ioctl_wlan_get_channel(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_wlan_get_channel, IOCTL_WLAN_GET_CHANNEL, mx_handle_t);

// TODO(tkilbourn): wlan messages defined elsewhere

// DEPRECATED: use IOCTL_WLAN_GET_CHANNEL and use request/response protocol on
// the channel instead.
//
// All of these interfaces are unstable and subject to change without notice.
//
// Start scanning for wireless networks. Scan reports are sent back on the
// channel that is returned from this ioctl. When all channels are closed, the
// device stops scanning.
//   in: wlan_start_scan_args
//   out: mx_handle_t (channel)
#define IOCTL_WLAN_START_SCAN \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_WLAN, 1)

#define WLAN_SCANTYPE_PASSIVE 0
#define WLAN_SCANTYPE_ACTIVE  1

#define WLAN_BSSTYPE_INFRASTRUCTURE 0
#define WLAN_BSSTYPE_INDEPENDENT 1
// TODO: fill in some more of these
#define WLAN_BSSTYPE_UNKNOWN 99

typedef struct {
    // The specific bssid to scan for, or the wildcard bssid (all zeros).
    uint8_t bssid[6];
    // The specific ssid to scan for, or the wildcard ssid (all zeros).
    uint8_t ssid[32];
    // The length of the ssid to scan for.
    size_t ssid_len;
    // Whether to do an active or a passive scan.
    uint8_t scan_type;
    // Delay in microseconds before sending a probe request during active
    // scanning.
    uint16_t probe_delay;
    // Minimum time to spend on a channel during scanning, in wlan time-units.
    uint16_t min_channel_time;
    // Maximum time to spend on a channel during scanning, in wlan time-units.
    uint16_t max_channel_time;
    // Number of channels to scan. Zero means scan all available channels for
    // the wlan device.
    uint16_t num_channels;
    // Channels to scan. If no channels are specified, all available channels
    // for the wlan device are scanned. Invalid channels are ignored.
    uint16_t channels[0];
    // etc.
} wlan_start_scan_args;

typedef struct {
    // The bssid of that was found.
    uint8_t bssid[6];
    // The type of the bss that was found.
    uint32_t bss_type;
    // Timestamp from the scan.
    uint64_t timestamp;
    // The period at which beacons are sent, in wlan time-units.
    uint16_t beacon_period;
    // Capabilities of the bss. (TODO)
    uint16_t capabilities;
    // The ssid name.
    uint8_t ssid[32];
    // Length of the ssid name.
    size_t ssid_len;
    // The basic supported rates for the wlan.
    uint8_t supported_rates[8];
    // etc.
} wlan_scan_report;

// ssize_t ioctl_wlan_start_scan(int fd, const wlan_start_scan_args* in, size_t in_len, mx_handle_t* out);
IOCTL_WRAPPER_VARIN_OUT(ioctl_wlan_start_scan, IOCTL_WLAN_START_SCAN, wlan_start_scan_args, mx_handle_t);

__END_CDECLS
