// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan_test

import (
	mlme "fidl/wlan_mlme"
	"testing"
	. "wlan/wlan"
)

func addBss(index int, ssid string, channel uint8, cbw mlme.Cbw, rssi int8, resp *mlme.ScanConfirm) {
	bssDesc := mlme.BssDescription{
		Bssid:   [6]uint8{uint8(index), 1, 2, 3, 4, 5},
		Ssid:    ssid,
		BssType: mlme.BssTypesInfrastructure,
		Chan: mlme.WlanChan{
			Primary: channel,
			Cbw:     cbw,
		},
		RssiDbm: rssi,
	}
	resp.BssDescriptionSet = append(resp.BssDescriptionSet, bssDesc)
}

func TestCollectResults(t *testing.T) {
	resp := &mlme.ScanConfirm{}
	addBss(0, "abc", 1, mlme.CbwCbw20, -60, resp)
	addBss(1, "def", 1, mlme.CbwCbw20, -60, resp)
	addBss(2, "abc", 6, mlme.CbwCbw20, -65, resp)

	aps := CollectScanResults(resp, "abc", "")
	if len(aps) != 2 {
		t.Fatalf("Failed to collect 2 results for SSID \"abc\" (found %d)", len(aps))
	}
	ap := aps[0]
	if ap.RssiDbm != -60 {
		t.Fatalf("Failed to find AP with best RSSI")
	}

	bssid_to_search := "02:01:02:03:04:05"
	aps = CollectScanResults(resp, "abc", bssid_to_search)
	if len(aps) != 1 {
		t.Fatalf("Failed to collect 1 results for SSID \"abc\" and BSSID %s (found %d)",
			bssid_to_search, len(aps))
	}

}
