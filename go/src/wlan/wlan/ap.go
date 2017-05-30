// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import mlme "apps/wlan/services/wlan_mlme"

type AP struct {
	BSSID    [6]uint8
	SSID     string
	BSSDesc  *mlme.BssDescription
	LastRSSI uint8

	state    State
}

func NewAP(bssDesc *mlme.BssDescription) *AP {
	return &AP{
		BSSID:    bssDesc.Bssid,
		SSID:     bssDesc.Ssid,
		BSSDesc:  bssDesc,
		LastRSSI: 0xff,
		state:    StateUnknown,
	}
}
