// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"fidl/fuchsia/wlan/mlme"
	"fmt"
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
		bssType = fmt.Sprintf("unknown (%v)", bss.BssType)
	}
	log.Print("    Type: ", bssType)
	log.Print("    Beacon period: ", bss.BeaconPeriod)
	log.Print("    DTIM period: ", bss.DtimPeriod)
	log.Print("    Timestamp: ", bss.Timestamp)
	log.Print("    Local time: ", bss.LocalTime)
	// TODO(porce): Stringfy CBW
	log.Printf("    Channel: %d CBW: %d", bss.Chan.Primary, bss.Chan.Cbw)
	log.Printf("    RSSI: %d dBm", bss.RssiDbm)
	log.Printf("    RCPI: %.1f dBm", float32(bss.RcpiDbmh)/2.0)
	log.Printf("    RSNI: %.1f dB", float32(bss.RsniDbh)/2.0)
}

func PrintScanConfirm(resp *mlme.ScanConfirm) {
	log.Print("ScanConfirm")
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

func PrintJoinConfirm(resp *mlme.JoinConfirm) {
	log.Print("JoinConfirm")
	var resCode string
	switch resp.ResultCode {
	case mlme.JoinResultCodesSuccess:
		resCode = "Success"
	case mlme.JoinResultCodesJoinFailureTimeout:
		resCode = "Join failure timeout"
	}
	log.Print("  Result code: ", resCode)
}

func PrintAuthenticateConfirm(resp *mlme.AuthenticateConfirm) {
	log.Print("AuthenticateConfirm")
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

func PrintAssociateConfirm(resp *mlme.AssociateConfirm) {
	log.Print("AssociateConfirm")
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

func PrintDeauthenticateConfirm(ind *mlme.DeauthenticateConfirm) {
	log.Print("DeauthenticateConfirm")
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
	log.Printf("  RSSI: %d", int8(ind.RssiDbm))
}

func PrintDeviceQueryConfirm(resp *mlme.DeviceQueryConfirm) {
	log.Print("DeviceQueryConfirm")
	log.Printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x",
		resp.MacAddr[0], resp.MacAddr[1], resp.MacAddr[2],
		resp.MacAddr[3], resp.MacAddr[4], resp.MacAddr[5])
	log.Print("  Role:")
	switch resp.Role {
	case mlme.MacRoleClient:
		log.Print("    CLIENT")
	case mlme.MacRoleAp:
		log.Print("    AP")
	default:
		log.Printf("    Unknown(%v)", resp.Role)
	}
	for i, band := range resp.Bands {
		log.Printf("  Band %v:", i)
		log.Printf("    Basic rates: %v", band.BasicRates)
		log.Printf("    Base frequency: %v", band.BaseFrequency)
		log.Printf("    Channels: %v", band.Channels)
	}
}
