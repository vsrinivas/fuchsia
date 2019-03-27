# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


# These are a set of temporarily allowed exceptions to the area dependency
# rules. This data is a mapping from areas to target prefixes.  Any target
# within an area is allowed to depend on any target whose label starts with a
# prefix in that area's list.
exceptions = {
    '//src/cobalt': [
        '//garnet/lib/wlan/mlme:mlme',
    ],
    '//src/connectivity/overnet': [
        '//garnet/examples/fidl/services:echo',
    ],
    '//src/connectivity/telephony': [
        '//zircon/system/fidl/fuchsia-hardware-telephony-transport',
    ],
    '//src/developer': [
         '//garnet/lib/process:process',
         '//garnet/third_party/libunwindstack:libunwindstack',
    ],
    '//src/ledger': [
        '//peridot/lib',
        '//peridot/third_party/modp_b64:modp_b64',
        '//src/connectivity/overnet/lib/protocol:fidl_stream',
    ],
    '//src/ledger/bin/testing/ledger_test_instance_provider': [
        '//peridot/lib',
        '//src/ledger/bin/fidl:fidl',
    ],
    '//src/media/playback/mediaplayer': [
        '//garnet/bin/http:errors',
    ],
}
