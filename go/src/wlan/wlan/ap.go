// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

type AP struct {
	BSSID    [6]uint8
	SSID     string
	state    State
	LastRSSI uint8
}

func NewAP(bssid [6]byte, ssid string) *AP {
	return &AP{
		BSSID:    bssid,
		SSID:     ssid,
		state:    StateUnknown,
		LastRSSI: 0xff,
	}
}
