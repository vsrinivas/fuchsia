// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <src/connectivity/wlan/drivers/wlanif/device.h>

bool multicast_promisc_enabled = false;

static zx_status_t hook_set_multicast_promisc(void* ctx, bool enable) {
    multicast_promisc_enabled = enable;
    return ZX_OK;
}

// Verify that receiving an ethernet SetParam for multicast promiscuous mode results in a call to
// wlanif_impl->set_muilticast_promisc.
TEST(MulticastPromiscMode, OnOff) {
    zx_status_t status;

    wlanif_impl_protocol_ops_t proto_ops = {
       .set_multicast_promisc = hook_set_multicast_promisc
    };
    wlanif_impl_protocol_t proto = {
        .ops = &proto_ops
    };
    wlanif::Device device(nullptr, proto);

    multicast_promisc_enabled = false;

    // Disable => Enable
    status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(multicast_promisc_enabled, true);

    // Enable => Enable
    status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(multicast_promisc_enabled, true);

    // Enable => Enable (any non-zero value should be treated as "true")
    status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0x80, nullptr, 0);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(multicast_promisc_enabled, true);

    // Enable => Disable
    status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 0, nullptr, 0);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(multicast_promisc_enabled, false);
}

// Verify that we get a ZX_ERR_UNSUPPORTED back if the set_multicast_promisc hook is unimplemented.
TEST(MulticastPromiscMode, Unimplemented) {
    zx_status_t status;

    wlanif_impl_protocol_ops_t proto_ops = {};
    wlanif_impl_protocol_t proto = {
        .ops = &proto_ops
    };
    wlanif::Device device(nullptr, proto);

    multicast_promisc_enabled = false;

    status = device.EthSetParam(ETHERNET_SETPARAM_MULTICAST_PROMISC, 1, nullptr, 0);
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
    EXPECT_EQ(multicast_promisc_enabled, false);
}
