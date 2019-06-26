// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"net"
	"testing"

	"netstack/dns"

	fidlnet "fidl/fuchsia/net"

	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
	"golang.org/x/net/dns/dnsmessage"
)

func TestLookupHostname(t *testing.T) {
	t.Run("Lookup Hostname", func(t *testing.T) {
		ns := stack.New(
			[]string{
				ipv4.ProtocolName,
				ipv6.ProtocolName,
				arp.ProtocolName,
			}, nil, stack.Options{})
		dnsClient := dns.NewClient(ns)
		ni := nameLookupImpl{dnsClient: dnsClient}

		var v4loopback fidlnet.IpAddress
		v4loopback.SetIpv4(fidlnet.Ipv4Address{
			Addr: [4]uint8{127, 0, 0, 1},
		})
		host, err := ni.LookupHostname(v4loopback)
		AssertNoError(t, err)
		if host.Which() == fidlnet.NameLookupLookupHostnameResultErr {
			t.Errorf("LookupHostname(%s) returned error", net.IP(v4loopback.Ipv4.Addr[:]))
		}
		if host.Response.Hostname != "localhost" {
			t.Errorf("LookupHostname(%s) != localhost (got %s)", net.IP(v4loopback.Ipv4.Addr[:]), host.Response.Hostname)
		}

		var v6loopbackBytes [16]uint8
		copy(v6loopbackBytes[:], net.IPv6loopback)
		var v6loopback fidlnet.IpAddress
		v6loopback.SetIpv6(fidlnet.Ipv6Address{Addr: v6loopbackBytes})
		host, err = ni.LookupHostname(v6loopback)
		AssertNoError(t, err)
		if host.Which() == fidlnet.NameLookupLookupHostnameResultErr {
			t.Errorf("LookupHostname(%s) returned error", net.IP(v6loopback.Ipv6.Addr[:]))
		}
		if host.Response.Hostname != "localhost" {
			t.Errorf("LookupHostname(%s) != localhost (got %s)", net.IP(v6loopback.Ipv6.Addr[:]), host.Response.Hostname)
		}
	})
}

func TestLookupIp(t *testing.T) {
	t.Run("Lookup IP", func(t *testing.T) {
		ns := stack.New(
			[]string{
				ipv4.ProtocolName,
				ipv6.ProtocolName,
				arp.ProtocolName,
			}, nil, stack.Options{})
		dnsClient := dns.NewClient(ns)
		testaddr := [4]byte{192, 0, 2, 1}
		dnsClient.SetResolver(func(c *dns.Client, question dnsmessage.Question) (dnsmessage.Name, []dnsmessage.Resource, dnsmessage.Message, error) {
			r := dnsmessage.Message{
				Header: dnsmessage.Header{
					ID:       0,
					Response: true,
				},
				Questions: []dnsmessage.Question{question},
			}

			if question.Type == dnsmessage.TypeA {
				r.Answers = []dnsmessage.Resource{
					{
						Header: dnsmessage.ResourceHeader{
							Name:   question.Name,
							Type:   dnsmessage.TypeA,
							Class:  dnsmessage.ClassINET,
							Length: 4,
						},
						Body: &dnsmessage.AResource{
							A: testaddr,
						},
					},
				}
			}

			return question.Name, r.Answers, r, nil
		})
		ni := nameLookupImpl{dnsClient: dnsClient}

		testHostname := "example.com"
		ip, err := ni.LookupIp(testHostname, fidlnet.LookupIpOptionsV4Addrs)
		AssertNoError(t, err)
		if ip.Which() == fidlnet.NameLookupLookupIpResultErr {
			t.Errorf("LookupIp('%s') returned error", testHostname)
		}
		if len(ip.Response.Addr.Ipv4Addrs) != 1 {
			t.Fatalf("LookupIp('%s') returned no results", testHostname)
		}
		if got := ip.Response.Addr.Ipv4Addrs[0].Addr; got != testaddr {
			t.Errorf("got LookupIP(%s) = %s, want = %s", testHostname, got, testaddr)
		}
	})
}
