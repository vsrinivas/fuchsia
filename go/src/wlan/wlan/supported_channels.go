// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

var supportedChannels = []uint8{
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
	36, 40, 44, 48,
	// TODO(tkilbourn): enable DFS channels
	// 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165,
}

var supportedChannelMap map[uint8]struct{}

func init() {
	supportedChannelMap = make(map[uint8]struct{})
	for _, ch := range supportedChannels {
		supportedChannelMap[ch] = struct{}{}
	}
}
