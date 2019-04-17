# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


# These are a set of temporarily allowed exceptions to the area dependency
# rules. This data is a mapping from areas to target prefixes.  Any target
# within an area is allowed to depend on any target whose label starts with a
# prefix in that area's list.
exceptions = {
    '//src/cobalt': [
        '//src/connectivity/wlan/lib/mlme/cpp:mlme',
    ],
    '//src/connectivity/network': [
        '//garnet/go/src/fidlext/fuchsia/hardware/ethernet:fidlext_fuchsia_hardware_ethernet_fake_lib',
        '//garnet/go/src/grand_unified_binary:grand_unified_binary',
        '//garnet/lib/inet:inet',
        '//garnet/public/go/third_party',
        '//src/connectivity/network/testing/netemul/lib/network:ethertap',
    ],
    '//src/connectivity/network/netstack3': [
        '//garnet/lib/rust',
    ],
    '//src/connectivity/network/testing/netemul/runner': [
        '//garnet/lib/cmx:cmx',
        '//garnet/lib/process:process',
        '//garnet/lib/rust/',
    ],
    '//src/connectivity/overnet': [
        '//garnet/examples/fidl/services:echo',
    ],
    '//src/connectivity/telephony': [
        '//zircon/system/fidl/fuchsia-hardware-telephony-transport',
    ],
    '//src/connectivity/wlan': [
        '//garnet/lib/rust/connectivity-testing',
        '//garnet/lib/rust/ethernet',
        '//garnet/lib/wlan/',
        '//src/connectivity/wlan/drivers/lib',
        '//src/connectivity/wlan/drivers/testing',
    ],
    '//src/connectivity/wlan/drivers': [
        '//garnet/lib/wlan/',
        '//garnet/lib/rust/fuchsia-wlan-dev',
    ],
    '//src/connectivity/wlan/tools/wlantool': [
        '//garnet/lib/wlan/',
    ],
    '//src/connectivity/wlan/wlancfg': [
        '//garnet/lib/wlan/',
    ],
    '//src/connectivity/wlan/wlanstack': [
        '//garnet/lib/wlan/',
        '//garnet/lib/rust/fuchsia-wlan-dev',
        '//src/connectivity/wlan/testing/wlantap-client',
    ],
    '//src/developer/debug': [
         '//garnet/lib/process:process',
         '//garnet/third_party/libunwindstack:libunwindstack',
         '//garnet/third_party/llvm',
    ],
    '//src/ledger': [
        '//peridot/lib/',
        '//peridot/third_party/modp_b64:modp_b64',
    ],
    '//src/ledger/bin/testing/ledger_test_instance_provider': [
        '//peridot/lib/',
        '//src/ledger/bin/fidl:fidl',
    ],
    '//src/media/playback/mediaplayer': [
        '//garnet/bin/http:errors',
    ],
}
