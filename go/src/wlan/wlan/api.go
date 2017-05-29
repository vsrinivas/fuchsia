// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	mlme "apps/wlan/services/wlan_mlme"
	bindings "fidl/bindings"
	"fmt"
	"log"
)

type APIHeader struct {
	txid    uint64
	flags   uint32
	ordinal int32
}

func (h *APIHeader) Decode(decoder *bindings.Decoder) error {
	_, err := decoder.StartStruct()
	if err != nil {
		return err
	}
	defer decoder.Finish()

	if h.txid, err = decoder.ReadUint64(); err != nil {
		return err
	}
	if h.flags, err = decoder.ReadUint32(); err != nil {
		return err
	}
	h.ordinal, err = decoder.ReadInt32()
	return err
}

func PrintBssDescription(bss *mlme.BssDescription) {
	log.Printf("  * BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
		bss.Bssid[0], bss.Bssid[1], bss.Bssid[2], bss.Bssid[3], bss.Bssid[4], bss.Bssid[5])
	log.Print("    SSID: ", bss.Ssid)
	var bssType string
	switch bss.BssType {
	case mlme.BssTypes_Infrastructure:
		bssType = "Infrastructure"
	case mlme.BssTypes_Personal:
		bssType = "Personal"
	case mlme.BssTypes_Independent:
		bssType = "Independent"
	case mlme.BssTypes_Mesh:
		bssType = "Mesh"
	default:
		bssType = fmt.Sprint("unknown (%v)", bss.BssType)
	}
	log.Print("    Type: ", bssType)
	log.Print("    Beacon period: ", bss.BeaconPeriod)
	log.Print("    DTIM period: ", bss.DtimPeriod)
	log.Print("    Timestamp: ", bss.Timestamp)
	log.Print("    Local time: ", bss.LocalTime)
	log.Print("    Channel: ", bss.Channel)
	if bss.RssiMeasurement != 0xff {
		log.Printf("    RSSI: %d dBm", int8(bss.RssiMeasurement))
	}
	if bss.RcpiMeasurement != 0xff {
		var rcpiStr string
		if bss.RcpiMeasurement == 0 {
			rcpiStr = "RCPI: < -109.5 dBm"
		} else if bss.RcpiMeasurement == 220 {
			rcpiStr = "RCPI: >= 0 dBm"
		} else if bss.RcpiMeasurement > 220 {
			rcpiStr = "RCPI: invalid"
		} else {
			rcpiStr = fmt.Sprintf("RCPI: %.1f dBm", float32(bss.RcpiMeasurement) / 2 - 110)
		}
		log.Print("    ", rcpiStr)
	}
	if bss.RsniMeasurement != 0xff {
		log.Printf("    RSNI: %.1f dBm", float32(int8(bss.RsniMeasurement)) / 2 - 10)
	}
}

func PrintScanResponse(resp *mlme.ScanResponse) {
	log.Print("ScanResponse")
	var resCode string
	switch resp.ResultCode {
	case mlme.ResultCodes_Success:
		resCode = "Success"
	case mlme.ResultCodes_NotSupported:
		resCode = "Not supported"
	}
	log.Print("  Result code: ", resCode)
	log.Print("  BSS descriptions:")
	for _, bss := range resp.BssDescriptionSet {
		PrintBssDescription(&bss)
	}
}
