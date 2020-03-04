package main

/*
#include "dnssdfinder.h"
*/
import "C"

import (
	"fmt"
)

type dnsSDError int32

const (
	dnsSDNoError                   = dnsSDError(C.kDNSServiceErr_NoError)
	dnsSDUnknown                   = dnsSDError(C.kDNSServiceErr_Unknown)
	dnsSDNoSuchName                = dnsSDError(C.kDNSServiceErr_NoSuchName)
	dnsSDNoMemory                  = dnsSDError(C.kDNSServiceErr_NoMemory)
	dnsSDBadParam                  = dnsSDError(C.kDNSServiceErr_BadParam)
	dnsSDBadReference              = dnsSDError(C.kDNSServiceErr_BadReference)
	dnsSDBadState                  = dnsSDError(C.kDNSServiceErr_BadState)
	dnsSDBadFlags                  = dnsSDError(C.kDNSServiceErr_BadFlags)
	dnsSDUnsupported               = dnsSDError(C.kDNSServiceErr_Unsupported)
	dnsSDNotInitialized            = dnsSDError(C.kDNSServiceErr_NotInitialized)
	dnsSDAlreadyRegistered         = dnsSDError(C.kDNSServiceErr_AlreadyRegistered)
	dnsSDNameConflict              = dnsSDError(C.kDNSServiceErr_NameConflict)
	dnsSDInvalid                   = dnsSDError(C.kDNSServiceErr_Invalid)
	dnsSDFirewall                  = dnsSDError(C.kDNSServiceErr_Firewall)
	dnsSDIncompatible              = dnsSDError(C.kDNSServiceErr_Incompatible)
	dnsSDBadInterfaceIndex         = dnsSDError(C.kDNSServiceErr_BadInterfaceIndex)
	dnsSDRefused                   = dnsSDError(C.kDNSServiceErr_Refused)
	dnsSDNoSuchRecord              = dnsSDError(C.kDNSServiceErr_NoSuchRecord)
	dnsSDNoAuth                    = dnsSDError(C.kDNSServiceErr_NoAuth)
	dnsSDNoSuchKey                 = dnsSDError(C.kDNSServiceErr_NoSuchKey)
	dnsSDNATTraversal              = dnsSDError(C.kDNSServiceErr_NATTraversal)
	dnsSDDoubleNAT                 = dnsSDError(C.kDNSServiceErr_DoubleNAT)
	dnsSDBadTime                   = dnsSDError(C.kDNSServiceErr_BadTime)
	dnsSDBadSig                    = dnsSDError(C.kDNSServiceErr_BadSig)
	dnsSDBadKey                    = dnsSDError(C.kDNSServiceErr_BadKey)
	dnsSDTransient                 = dnsSDError(C.kDNSServiceErr_Transient)
	dnsSDServiceNotRunning         = dnsSDError(C.kDNSServiceErr_ServiceNotRunning)
	dnsSDNATPortMappingUnsupported = dnsSDError(C.kDNSServiceErr_NATPortMappingUnsupported)
	dnsSDNATPortMappingDisabled    = dnsSDError(C.kDNSServiceErr_NATPortMappingDisabled)
	dnsSDNoRouter                  = dnsSDError(C.kDNSServiceErr_NoRouter)
	dnsSDPollingMode               = dnsSDError(C.kDNSServiceErr_PollingMode)
	dnsSDTimeout                   = dnsSDError(C.kDNSServiceErr_Timeout)
)

func (d dnsSDError) Error() string {
	switch d {
	case dnsSDNoError:
		return "NoError"
	case dnsSDUnknown:
		return "Unknown"
	case dnsSDNoSuchName:
		return "NoSuchName"
	case dnsSDNoMemory:
		return "NoMemory"
	case dnsSDBadParam:
		return "BadParam"
	case dnsSDBadReference:
		return "BadReference"
	case dnsSDBadState:
		return "BadState"
	case dnsSDBadFlags:
		return "BadFlags"
	case dnsSDUnsupported:
		return "Unsupported"
	case dnsSDNotInitialized:
		return "NotInitialized"
	case dnsSDAlreadyRegistered:
		return "AlreadyRegistered"
	case dnsSDNameConflict:
		return "NameConflict"
	case dnsSDInvalid:
		return "Invalid"
	case dnsSDFirewall:
		return "Firewall"
	case dnsSDIncompatible:
		return "Incompatible"
	case dnsSDBadInterfaceIndex:
		return "BadInterfaceIndex"
	case dnsSDRefused:
		return "Refused"
	case dnsSDNoSuchRecord:
		return "NoSuchRecord"
	case dnsSDNoAuth:
		return "NoAuth"
	case dnsSDNoSuchKey:
		return "NoSuchKey"
	case dnsSDNATTraversal:
		return "NATTraversal"
	case dnsSDDoubleNAT:
		return "DoubleNAT"
	case dnsSDBadTime:
		return "BadTime"
	case dnsSDBadSig:
		return "BadSig"
	case dnsSDBadKey:
		return "BadKey"
	case dnsSDTransient:
		return "Transient"
	case dnsSDServiceNotRunning:
		return "ServiceNotRunning"
	case dnsSDNATPortMappingUnsupported:
		return "NATPortMappingUnsupported"
	case dnsSDNATPortMappingDisabled:
		return "NATPortMappingDisabled"
	case dnsSDNoRouter:
		return "NoRouter"
	case dnsSDPollingMode:
		return "PollingMode"
	case dnsSDTimeout:
		return "Timeout"
	default:
		return fmt.Sprintf("Unrecognized Error Code: %d", d)
	}
}

func (d dnsSDError) Is(other error) bool {
	otherConv, ok := other.(dnsSDError)
	if !ok {
		return false
	}
	return otherConv == d
}
