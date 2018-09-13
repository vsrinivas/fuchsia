// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"fmt"
	"net"

	"syslog"

	"netstack/dns"

	fidlnet "fidl/fuchsia/net"

	"gvisor.dev/gvisor/pkg/tcpip"
)

type nameLookupImpl struct {
	dnsClient *dns.Client
}

var _ fidlnet.NameLookup = (*nameLookupImpl)(nil)

func (sp *nameLookupImpl) LookupIp(hostname string, options fidlnet.LookupIpOptions) (fidlnet.NameLookupLookupIpResult, error) {
	var result fidlnet.NameLookupLookupIpResult
	var response fidlnet.NameLookupLookupIpResponse

	var ips []tcpip.Address

	if hostname == "localhost" {
		ips = []tcpip.Address{
			ipv4Loopback,
			ipv6Loopback,
		}
	} else {
		var err error
		ips, err = sp.dnsClient.LookupIP(hostname)
		if err != nil {
			syslog.VLogTf(syslog.DebugVerbosity, "DNS", "lookup failed with internal error: %s", err)
			result.SetErr(fidlnet.LookupErrorInternalError)
			return result, nil
		}
		if len(ips) == 0 {
			result.SetErr(fidlnet.LookupErrorNotFound)
			return result, nil
		}
	}

	for _, ip := range ips {
		// TODO(wesleyac): Consider handling IPv4-mapped addresses as IPv4 addresses
		if ip4 := ip.To4(); ip4 != "" && (options&fidlnet.LookupIpOptionsV4Addrs) != 0 {
			var addr fidlnet.Ipv4Address
			copy(addr.Addr[:], ip4)
			response.Addr.Ipv4Addrs = append(response.Addr.Ipv4Addrs, addr)
		} else if options&fidlnet.LookupIpOptionsV6Addrs != 0 {
			var addr fidlnet.Ipv6Address
			copy(addr.Addr[:], ip)
			response.Addr.Ipv6Addrs = append(response.Addr.Ipv6Addrs, addr)
		}
	}

	result.SetResponse(response)
	return result, nil
}

// This only supports localhost right now, since the netstack DNS client doesn't support reverse DNS.
func (sp *nameLookupImpl) LookupHostname(addr fidlnet.IpAddress) (fidlnet.NameLookupLookupHostnameResult, error) {
	var result fidlnet.NameLookupLookupHostnameResult
	switch typ := addr.Which(); typ {
	case fidlnet.IpAddressIpv4:
		if net.IP(addr.Ipv4.Addr[:]).IsLoopback() {
			result.SetResponse(fidlnet.NameLookupLookupHostnameResponse{Hostname: "localhost"})
			return result, nil
		}
	case fidlnet.IpAddressIpv6:
		if net.IP(addr.Ipv6.Addr[:]).IsLoopback() {
			result.SetResponse(fidlnet.NameLookupLookupHostnameResponse{Hostname: "localhost"})
			return result, nil
		}
	default:
		panic(fmt.Sprintf("unknown address type: %v", typ))
	}
	result.SetErr(fidlnet.LookupErrorNotFound)
	return result, nil
}
