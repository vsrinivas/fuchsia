// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"fmt"
	mlme "garnet/public/lib/wlan/fidl/wlan_mlme"
	"sort"
	"strings"
)

type AP struct {
	BSSID    [6]uint8
	SSID     string
	BSSDesc  *mlme.BssDescription
	LastRSSI uint8
}

func NewAP(bssDesc *mlme.BssDescription) *AP {
	b := *bssDesc // make a copy.
	return &AP{
		BSSID:    bssDesc.Bssid,
		SSID:     bssDesc.Ssid,
		BSSDesc:  &b,
		LastRSSI: 0xff,
	}
}

func macStr(macArray [6]uint8) string {
	return fmt.Sprintf("%02x:%02x:%02x:%02x:%02x:%02x",
		macArray[0], macArray[1], macArray[2], macArray[3], macArray[4], macArray[5])
}

func CollectScanResults(resp *mlme.ScanResponse, ssid string, bssid string) []AP {
	aps := []AP{}
	for _, s := range resp.BssDescriptionSet {
		if bssid != "" {
			// Match the specified BSSID only
			if macStr(s.Bssid) != strings.ToLower(bssid) {
				continue
			}
		}

		if s.Ssid == ssid || ssid == "" {
			ap := NewAP(&s)
			ap.LastRSSI = s.RssiMeasurement
			aps = append(aps, *ap)
		}
	}

	if len(resp.BssDescriptionSet) > 0 && len(aps) == 0 {
		fmt.Printf("wlan: no matching network among %d scanned\n", len(resp.BssDescriptionSet))
	}
	sort.Slice(aps, func(i, j int) bool { return int8(aps[i].LastRSSI) > int8(aps[j].LastRSSI) })
	return aps
}
