// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"syscall/zx/fidl"

	"fuchsia.googlesource.com/syslog"
	"netstack/dns"
	"netstack/fidlconv"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/name"

	"gvisor.dev/gvisor/pkg/tcpip"
)

const tag = "nameLookupAdmin"

func dnsServerToFidl(s dns.Server) name.DnsServer {
	return name.DnsServer{
		Address:        fidlconv.ToNetSocketAddress(s.Address),
		AddressPresent: true,
		Source:         s.Source,
		SourcePresent:  true,
	}
}

type nameLookupAdminImpl struct {
	ns *Netstack
}

var _ name.LookupAdminWithCtx = (*nameLookupAdminImpl)(nil)

func (dns *nameLookupAdminImpl) SetDefaultDnsServers(_ fidl.Context, servers []net.IpAddress) (name.LookupAdminSetDefaultDnsServersResult, error) {
	ss := make([]tcpip.Address, len(servers))

	for i, s := range servers {
		ss[i] = fidlconv.ToTCPIPAddress(s)
	}

	syslog.InfoTf(tag, "setting default name servers: %s", ss)
	dns.ns.dnsClient.SetDefaultServers(ss)
	// NOTE(brunodalbo) we're not bothering checking for invalid server addresses here in expectation that this
	// implementation will move to dns_resolver.
	return name.LookupAdminSetDefaultDnsServersResultWithResponse(name.LookupAdminSetDefaultDnsServersResponse{}), nil
}

func (dns *nameLookupAdminImpl) GetDnsServers(fidl.Context) ([]name.DnsServer, error) {
	cache := dns.ns.dnsClient.GetServersCache()
	servers := make([]name.DnsServer, 0, len(cache))
	for _, s := range cache {
		servers = append(servers, dnsServerToFidl(s))
	}
	return servers, nil
}
