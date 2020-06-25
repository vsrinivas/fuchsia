// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dns"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

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

// SetDnsServers implements name.LookupAdminWithCtx.SetDnsServers.
//
// This method does nothing as netstack learns DNS servers itself and configures
// the in-netstack DNS client directly. We're not bothering with a full implementation
// here in expectation that dns_resolver will be used for name resolution.
func (*nameLookupAdminImpl) SetDnsServers(fidl.Context, []net.SocketAddress) (name.LookupAdminSetDnsServersResult, error) {
	syslog.ErrorTf(tag, "SetDnsServers not implemented")
	return name.LookupAdminSetDnsServersResultWithErr(int32(zx.ErrNotSupported)), nil
}

func (dns *nameLookupAdminImpl) GetDnsServers(fidl.Context) ([]net.SocketAddress, error) {
	cache := dns.ns.dnsClient.GetServersCache()
	servers := make([]net.SocketAddress, 0, len(cache))
	for _, s := range cache {
		servers = append(servers, fidlconv.ToNetSocketAddress(s.Address))
	}
	return servers, nil
}
