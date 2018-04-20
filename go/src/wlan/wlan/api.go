// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"fmt"
	mlme "fuchsia/go/wlan_mlme"
	"log"
)

func PrintBssDescription(bss *mlme.BssDescription) {
	log.Printf("  * BSSID: %02x:%02x:%02x:%02x:%02x:%02x",
		bss.Bssid[0], bss.Bssid[1], bss.Bssid[2], bss.Bssid[3], bss.Bssid[4], bss.Bssid[5])
	log.Print("    SSID: ", bss.Ssid)
	var bssType string
	switch bss.BssType {
	case mlme.BssTypesInfrastructure:
		bssType = "Infrastructure"
	case mlme.BssTypesPersonal:
		bssType = "Personal"
	case mlme.BssTypesIndependent:
		bssType = "Independent"
	case mlme.BssTypesMesh:
		bssType = "Mesh"
	default:
		bssType = fmt.Sprint("unknown (%v)", bss.BssType)
	}
	log.Print("    Type: ", bssType)
	log.Print("    Beacon period: ", bss.BeaconPeriod)
	log.Print("    DTIM period: ", bss.DtimPeriod)
	log.Print("    Timestamp: ", bss.Timestamp)
	log.Print("    Local time: ", bss.LocalTime)
	// TODO(porce): Stringfy CBW
	log.Printf("    Channel: %u CBW: %u", bss.Chan.Primary, bss.Chan.Cbw)
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
	case mlme.ScanResultCodesSuccess:
		resCode = "Success"
	case mlme.ScanResultCodesNotSupported:
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
	case mlme.JoinResultCodesSuccess:
		resCode = "Success"
	case mlme.JoinResultCodesJoinFailureTimeout:
		resCode = "Join failure timeout"
	}
	log.Print("  Result code: ", resCode)
}

func PrintAuthenticateResponse(resp *mlme.AuthenticateResponse) {
	log.Print("AuthenticateResponse")
	var authType string
	switch resp.AuthType {
	case mlme.AuthenticationTypesOpenSystem:
		authType = "Open"
	case mlme.AuthenticationTypesSharedKey:
		authType = "Shared key"
	case mlme.AuthenticationTypesFastBssTransition:
		authType = "Fast BSS transition"
	case mlme.AuthenticationTypesSae:
		authType = "SAE"
	}
	log.Print("  Authentication type: ", authType)
	var resCode string
	switch resp.ResultCode {
	case mlme.AuthenticateResultCodesSuccess:
		resCode = "Success"
	case mlme.AuthenticateResultCodesRefused:
		resCode = "Refused"
	case mlme.AuthenticateResultCodesAntiCloggingTokenRequired:
		resCode = "Anti-clogging token required"
	case mlme.AuthenticateResultCodesFiniteCyclicGroupNotSupported:
		resCode = "Finite cyclic group not supported"
	case mlme.AuthenticateResultCodesAuthenticationRejected:
		resCode = "Authentication rejected"
	case mlme.AuthenticateResultCodesAuthFailureTimeout:
		resCode = "Authentication failure timeout"
	}
	log.Print("  Result code: ", resCode)
}

func PrintAssociateResponse(resp *mlme.AssociateResponse) {
	log.Print("AssociateResponse")
	var resCode string
	switch resp.ResultCode {
	case mlme.AssociateResultCodesSuccess:
		resCode = "Success"
	case mlme.AssociateResultCodesRefusedReasonUnspecified:
		resCode = "Refused (unspecified)"
	case mlme.AssociateResultCodesRefusedNotAuthenticated:
		resCode = "Refused (not authenticated)"
	case mlme.AssociateResultCodesRefusedCapabilitiesMismatch:
		resCode = "Refused (capabilities mismatch)"
	case mlme.AssociateResultCodesRefusedExternalReason:
		resCode = "Refused (external reason)"
	case mlme.AssociateResultCodesRefusedApOutOfMemory:
		resCode = "Refused (AP out of memory)"
	case mlme.AssociateResultCodesRefusedBasicRatesMismatch:
		resCode = "Refused (basic rates mismatch)"
	case mlme.AssociateResultCodesRejectedEmergencyServicesNotSupported:
		resCode = "Rejected (emergency services not supported)"
	case mlme.AssociateResultCodesRefusedTemporarily:
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

func PrintDeauthenticateResponse(ind *mlme.DeauthenticateResponse) {
	log.Print("DeauthenticateResponse")
	log.Printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		ind.PeerStaAddress[0], ind.PeerStaAddress[1], ind.PeerStaAddress[2],
		ind.PeerStaAddress[3], ind.PeerStaAddress[4], ind.PeerStaAddress[5])
}

func PrintDeauthenticateIndication(ind *mlme.DeauthenticateIndication) {
	log.Print("DeauthenticateIndication")
	log.Printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		ind.PeerStaAddress[0], ind.PeerStaAddress[1], ind.PeerStaAddress[2],
		ind.PeerStaAddress[3], ind.PeerStaAddress[4], ind.PeerStaAddress[5])
	// TODO(tkilbourn): map reason codes to strings
	log.Print("  Reason code: ", ind.ReasonCode)

}

func PrintSignalReportIndication(ind *mlme.SignalReportIndication) {
	log.Print("SignalReportIndication")
	log.Printf("  RSSI: %d", int8(ind.Rssi))
}

func PrintDeviceQueryResponse(resp *mlme.DeviceQueryResponse) {
	log.Print("DeviceQueryResponse")
	log.Printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		resp.MacAddr[0], resp.MacAddr[1], resp.MacAddr[2],
		resp.MacAddr[3], resp.MacAddr[4], resp.MacAddr[5])
	log.Print("  Modes:")
	for _, mode := range resp.Modes {
		switch mode {
		case mlme.MacModeSta:
			log.Print("    STA")
		case mlme.MacModeAp:
			log.Print("    AP")
		default:
			log.Printf("    Unknown(%v)", mode)
		}
	}
	for i, band := range resp.Bands {
		log.Printf("  Band %v:", i)
		log.Printf("    Basic rates: %v", band.BasicRates)
		log.Printf("    Base frequency: %v", band.BaseFrequency)
		log.Printf("    Channels: %v", band.Channels)
	}
}
