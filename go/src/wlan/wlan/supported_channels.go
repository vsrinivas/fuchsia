// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

var supportedChannels = []uint8{
    // 5GHz UNII-1
    36, 40, 44, 48,

    // 5GHz UNII-2 Middle
    52, 56, 60, 64,

    // 5GHz UNII-2 Extended
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,

    // 5GHz UNII-3. TODO(NET-392)
    // 149, 153, 157, 161, 165,

    // 2GHz: Search after 5GHz band
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
}

var supportedChannelMap map[uint8]struct{}

func init() {
	supportedChannelMap = make(map[uint8]struct{})
	for _, ch := range supportedChannels {
		supportedChannelMap[ch] = struct{}{}
	}
}
