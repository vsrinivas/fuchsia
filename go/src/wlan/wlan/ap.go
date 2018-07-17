// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"fidl/fuchsia/wlan/mlme"
	"fmt"
	"sort"
	"strings"
	"wlan/eapol"
)

type AP struct {
	BSSID        [6]uint8
	SSID         string
	BSSDesc      *mlme.BssDescription
	RssiDbm      int8
	IsCompatible bool
	Chan         mlme.WlanChan
}

// ByRSSI implements sort.Interface for []AP based on the RssiDbm field.
type ByRSSI []AP

func (a ByRSSI) Len() int           { return len(a) }
func (a ByRSSI) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByRSSI) Less(i, j int) bool { return a[i].RssiDbm > a[j].RssiDbm }

func NewAP(bssDesc *mlme.BssDescription) *AP {
	b := *bssDesc // make a copy.
	return &AP{
		BSSID:   bssDesc.Bssid,
		SSID:    bssDesc.Ssid,
		BSSDesc: &b,
		RssiDbm: 0x0,
	}
}

func macStr(macArray [6]uint8) string {
	return fmt.Sprintf("%02x:%02x:%02x:%02x:%02x:%02x",
		macArray[0], macArray[1], macArray[2], macArray[3], macArray[4], macArray[5])
}

func IsBssCompatible(bss *mlme.BssDescription) bool {
	if bss.Rsn != nil && len(*bss.Rsn) > 0 {
		is_rsn_compatible, err := eapol.IsRSNSupported(*bss.Rsn)
		return is_rsn_compatible && (err == nil)
	}
	return true
}

func CollectScanResults(resp *mlme.ScanConfirm, ssid string, bssid string) []AP {
	aps := []AP{}
	for idx, s := range resp.BssDescriptionSet {
		if bssid != "" {
			// Match the specified BSSID only
			if macStr(s.Bssid) != strings.ToLower(bssid) {
				continue
			}
		}

		if s.Ssid == ssid || ssid == "" {
			ap := NewAP(&s)
			ap.RssiDbm = s.RssiDbm
			ap.IsCompatible = IsBssCompatible(&resp.BssDescriptionSet[idx])
			ap.Chan = resp.BssDescriptionSet[idx].Chan
			aps = append(aps, *ap)
		}
	}

	sort.Slice(aps, func(i, j int) bool { return aps[i].RssiDbm > aps[j].RssiDbm })
	return aps
}
