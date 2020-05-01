// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"testing"

	"netstack/dns"
	"netstack/fidlconv"

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

	expect := []name.DnsServer{{
		Address: net.SocketAddressWithIpv4(net.Ipv4SocketAddress{
			Address: addrv4,
			Port:    dns.DefaultDNSPort,
		}),
		AddressPresent: true,
		Source: name.DnsServerSource{
			I_dnsServerSourceTag: name.DnsServerSourceStaticSource,
		},
		SourcePresent: true,
	}, {
		Address: net.SocketAddressWithIpv6(net.Ipv6SocketAddress{
			Address:   addrv6,
			Port:      dns.DefaultDNSPort,
			ZoneIndex: 0,
		}),
		AddressPresent: true,
		Source: name.DnsServerSource{
			I_dnsServerSourceTag: name.DnsServerSourceStaticSource,
		},
		SourcePresent: true,
	}}

	if diff := cmp.Diff(servers, expect, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
		t.Fatalf("got unexpected list of servers (-want +got): %s", diff)
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

	expect := []name.DnsServer{
		// NDP
		{
			Address:        fidlconv.ToNetSocketAddress(ndpServer),
			AddressPresent: true,
			Source: name.DnsServerSource{
				I_dnsServerSourceTag: name.DnsServerSourceNdp,
				Ndp: name.NdpDnsServerSource{
					SourceInterface:        1,
					SourceInterfacePresent: true,
				},
			},
			SourcePresent: true,
		},
		// DHCP
		{
			Address: net.SocketAddressWithIpv4(net.Ipv4SocketAddress{
				Address: fidlconv.ToNetIpAddress(dhcpServer).Ipv4,
				Port:    dns.DefaultDNSPort,
			}),
			AddressPresent: true,
			Source: name.DnsServerSource{
				I_dnsServerSourceTag: name.DnsServerSourceDhcp,
				Dhcp: name.DhcpDnsServerSource{
					SourceInterface:        1,
					SourceInterfacePresent: true,
				},
			},
			SourcePresent: true,
		},
		// STATIC
		{
			Address: net.SocketAddressWithIpv4(net.Ipv4SocketAddress{
				Address: fidlconv.ToNetIpAddress(staticServer).Ipv4,
				Port:    dns.DefaultDNSPort,
			}),
			AddressPresent: true,
			Source: name.DnsServerSource{
				I_dnsServerSourceTag: name.DnsServerSourceStaticSource,
			},
			SourcePresent: true,
		}}

	if diff := cmp.Diff(result, expect, cmpopts.IgnoreTypes(struct{}{})); diff != "" {
		t.Fatalf("got unexpected list of servers (-want +got): %s", diff)
	}
}
