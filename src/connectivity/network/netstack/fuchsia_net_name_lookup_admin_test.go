// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/dns"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/fidlconv"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/name"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"gvisor.dev/gvisor/pkg/tcpip"
)

func newLookupAdminImpl(t *testing.T) *nameLookupAdminImpl {
	return &nameLookupAdminImpl{
		ns: newNetstack(t),
	}
}

func TestSetAndGetDefaultServers(t *testing.T) {
	lookupAdmin := newLookupAdminImpl(t)
	addrv4 := net.Ipv4Address{Addr: [4]uint8{8, 8, 8, 8}}
	addrv6 := net.Ipv6Address{Addr: [16]uint8{4, 4, 4, 4}}
	result, err := lookupAdmin.SetDefaultDnsServers(context.Background(), []net.IpAddress{
		net.IpAddressWithIpv4(addrv4),
		net.IpAddressWithIpv6(addrv6),
	})
	if err != nil {
		t.Fatalf("SetDefaultDnsServers failed: %s", err)
	}
	if result.Which() != name.LookupAdminSetDefaultDnsServersResultResponse {
		t.Fatalf("expected success response, got %+v", result)
	}
	servers, err := lookupAdmin.GetDnsServers(context.Background())
	if err != nil {
		t.Fatalf("GetDnsServers failed: %s", err)
	}

	expect := []net.SocketAddress{
		net.SocketAddressWithIpv4(net.Ipv4SocketAddress{
			Address: addrv4,
			Port:    dns.DefaultDNSPort,
		}),
		net.SocketAddressWithIpv6(net.Ipv6SocketAddress{
			Address:   addrv6,
			Port:      dns.DefaultDNSPort,
			ZoneIndex: 0,
		}),
	}

	if diff := cmp.Diff(expect, servers, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
		t.Fatalf("DNS servers mismatch (-want +got):\n%s", diff)
	}
}

func TestGetDynamicServers(t *testing.T) {
	staticServer := tcpip.Address("\x08\x08\x08\x08")
	dhcpServer := tcpip.Address("\x08\x08\x04\x04")
	ndpServer := tcpip.FullAddress{
		NIC:  1,
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
		Port: dns.DefaultDNSPort,
	}
	lookupAdmin := newLookupAdminImpl(t)
	lookupAdmin.ns.dnsClient.SetDefaultServers([]tcpip.Address{staticServer})
	lookupAdmin.ns.dnsClient.UpdateDhcpServers(1, &[]tcpip.Address{dhcpServer})
	lookupAdmin.ns.dnsClient.UpdateNdpServers([]tcpip.FullAddress{ndpServer}, -1)

	result, err := lookupAdmin.GetDnsServers(context.Background())
	if err != nil {
		t.Fatalf("GetDnsServers failed: %s", err)
	}

	expect := []net.SocketAddress{
		// NDP
		fidlconv.ToNetSocketAddress(ndpServer),

		// DHCP
		net.SocketAddressWithIpv4(net.Ipv4SocketAddress{
			Address: fidlconv.ToNetIpAddress(dhcpServer).Ipv4,
			Port:    dns.DefaultDNSPort,
		}),

		// STATIC
		net.SocketAddressWithIpv4(net.Ipv4SocketAddress{
			Address: fidlconv.ToNetIpAddress(staticServer).Ipv4,
			Port:    dns.DefaultDNSPort,
		}),
	}

	if diff := cmp.Diff(expect, result, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
		t.Fatalf("DNS servers mismatch (-want +got):\n%s", diff)
	}
}
