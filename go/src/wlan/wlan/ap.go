// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	mlme "garnet/public/lib/wlan/fidl/wlan_mlme"
	"sort"
)

type AP struct {
	BSSID    [6]uint8
	SSID     string
	BSSDesc  *mlme.BssDescription
	LastRSSI uint8
}

func NewAP(bssDesc *mlme.BssDescription) *AP {
	return &AP{
		BSSID:    bssDesc.Bssid,
		SSID:     bssDesc.Ssid,
		BSSDesc:  bssDesc,
		LastRSSI: 0xff,
	}
}

func CollectScanResults(resp *mlme.ScanResponse, ssid string) []AP {
	aps := []AP{}
	for _, s := range resp.BssDescriptionSet {
		if s.Ssid == ssid || ssid == "" {
			ap := NewAP(&s)
			ap.LastRSSI = s.RssiMeasurement
			aps = append(aps, *ap)
		}
	}
	sort.Slice(aps, func(i, j int) bool { return int8(aps[i].LastRSSI) > int8(aps[j].LastRSSI) })
	return aps
}
