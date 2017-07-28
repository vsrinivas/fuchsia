// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	mlme "apps/wlan/services/wlan_mlme"
	mlme_ext "apps/wlan/services/wlan_mlme_ext"
	bindings "fidl/bindings"
	"fmt"
	"log"
)

type APIHeader struct {
	txid    uint64
	flags   uint32
	ordinal int32
}

func (h *APIHeader) Encode(enc *bindings.Encoder) error {
	// Create a method call header, similar to that of FIDL2
	enc.StartStruct(16, 0)
	defer enc.Finish()

	if err := enc.WriteUint64(h.txid); err != nil {
		return fmt.Errorf("could not encode txid: %v", err)
	}
	if err := enc.WriteUint32(h.flags); err != nil {
		return fmt.Errorf("could not encode flags: %v", err)
	}
	if err := enc.WriteInt32(h.ordinal); err != nil {
		return fmt.Errorf("could not encode ordinal: %v", err)
	}
	return nil
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
			rcpiStr = fmt.Sprintf("RCPI: %.1f dBm", float32(bss.RcpiMeasurement)/2-110)
		}
		log.Print("    ", rcpiStr)
	}
	if bss.RsniMeasurement != 0xff {
		log.Printf("    RSNI: %.1f dBm", float32(int8(bss.RsniMeasurement))/2-10)
	}
}

func PrintScanResponse(resp *mlme.ScanResponse) {
	log.Print("ScanResponse")
	var resCode string
	switch resp.ResultCode {
	case mlme.ScanResultCodes_Success:
		resCode = "Success"
	case mlme.ScanResultCodes_NotSupported:
		resCode = "Not supported"
	}
	log.Print("  Result code: ", resCode)
	log.Print("  BSS descriptions:")
	for _, bss := range resp.BssDescriptionSet {
		PrintBssDescription(&bss)
	}
}

func PrintJoinResponse(resp *mlme.JoinResponse) {
	log.Print("JoinResponse")
	var resCode string
	switch resp.ResultCode {
	case mlme.JoinResultCodes_Success:
		resCode = "Success"
	case mlme.JoinResultCodes_JoinFailureTimeout:
		resCode = "Join failure timeout"
	}
	log.Print("  Result code: ", resCode)
}

func PrintAuthenticateResponse(resp *mlme.AuthenticateResponse) {
	log.Print("AuthenticateResponse")
	var authType string
	switch resp.AuthType {
	case mlme.AuthenticationTypes_OpenSystem:
		authType = "Open"
	case mlme.AuthenticationTypes_SharedKey:
		authType = "Shared key"
	case mlme.AuthenticationTypes_FastBssTransition:
		authType = "Fast BSS transition"
	case mlme.AuthenticationTypes_Sae:
		authType = "SAE"
	}
	log.Print("  Authentication type: ", authType)
	var resCode string
	switch resp.ResultCode {
	case mlme.AuthenticateResultCodes_Success:
		resCode = "Success"
	case mlme.AuthenticateResultCodes_Refused:
		resCode = "Refused"
	case mlme.AuthenticateResultCodes_AntiCloggingTokenRequired:
		resCode = "Anti-clogging token required"
	case mlme.AuthenticateResultCodes_FiniteCyclicGroupNotSupported:
		resCode = "Finite cyclic group not supported"
	case mlme.AuthenticateResultCodes_AuthenticationRejected:
		resCode = "Authentication rejected"
	case mlme.AuthenticateResultCodes_AuthFailureTimeout:
		resCode = "Authentication failure timeout"
	}
	log.Print("  Result code: ", resCode)
}

func PrintAssociateResponse(resp *mlme.AssociateResponse) {
	log.Print("AssociateResponse")
	var resCode string
	switch resp.ResultCode {
	case mlme.AssociateResultCodes_Success:
		resCode = "Success"
	case mlme.AssociateResultCodes_RefusedReasonUnspecified:
		resCode = "Refused (unspecified)"
	case mlme.AssociateResultCodes_RefusedNotAuthenticated:
		resCode = "Refused (not authenticated)"
	case mlme.AssociateResultCodes_RefusedCapabilitiesMismatch:
		resCode = "Refused (capabilities mismatch)"
	case mlme.AssociateResultCodes_RefusedExternalReason:
		resCode = "Refused (external reason)"
	case mlme.AssociateResultCodes_RefusedApOutOfMemory:
		resCode = "Refused (AP out of memory)"
	case mlme.AssociateResultCodes_RefusedBasicRatesMismatch:
		resCode = "Refused (basic rates mismatch)"
	case mlme.AssociateResultCodes_RejectedEmergencyServicesNotSupported:
		resCode = "Rejected (emergency services not supported)"
	case mlme.AssociateResultCodes_RefusedTemporarily:
		resCode = "Refused (temporarily)"
	}
	log.Print("  Result code: ", resCode)
	log.Print("  Association ID: ", resp.AssociationId)
}

func PrintDisassociateIndication(ind *mlme.DisassociateIndication) {
	log.Print("DisassociateIndication")
	log.Printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		ind.PeerStaAddress[0], ind.PeerStaAddress[1], ind.PeerStaAddress[2],
		ind.PeerStaAddress[3], ind.PeerStaAddress[4], ind.PeerStaAddress[5])
	// TODO(tkilbourn): map reason codes to strings
	log.Print("  Reason code: ", ind.ReasonCode)
}

func PrintDeauthenticateIndication(ind *mlme.DeauthenticateIndication) {
	log.Print("DeauthenticateIndication")
	log.Printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		ind.PeerStaAddress[0], ind.PeerStaAddress[1], ind.PeerStaAddress[2],
		ind.PeerStaAddress[3], ind.PeerStaAddress[4], ind.PeerStaAddress[5])
	// TODO(tkilbourn): map reason codes to strings
	log.Print("  Reason code: ", ind.ReasonCode)

}

func PrintSignalReportIndication(ind *mlme_ext.SignalReportIndication) {
	log.Print("SignalReportIndication")
	log.Printf("  RSSI: %d", ind.Rssi)
}
