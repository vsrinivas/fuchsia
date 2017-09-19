// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan_test

import (
	mlme "garnet/public/lib/wlan/fidl/wlan_mlme"
	"testing"
	. "wlan/wlan"
)

func addBss(index int, ssid string, channel uint8, rssi uint8, resp *mlme.ScanResponse) {
	bssDesc := mlme.BssDescription{
		Bssid:           [6]uint8{uint8(index), 1, 2, 3, 4, 5},
		Ssid:            ssid,
		BssType:         mlme.BssTypes_Infrastructure,
		Channel:         channel,
		RssiMeasurement: rssi,
	}
	resp.BssDescriptionSet = append(resp.BssDescriptionSet, bssDesc)
}

func TestCollectResults(t *testing.T) {
	resp := &mlme.ScanResponse{}
	addBss(0, "abc", 1, 0x80, resp)
	addBss(1, "def", 1, 0x80, resp)
	addBss(2, "abc", 6, 0x85, resp)

	aps := CollectScanResults(resp, "abc")
	if len(aps) != 2 {
		t.Fatalf("Failed to collect 2 results for SSID \"abc\" (found %d)", len(aps))
	}
	ap := aps[0]
	if ap.LastRSSI != 0x85 {
		t.Fatalf("Failed to find AP with best RSSI")
	}
}
