// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"net"
	"testing"

	"netstack/dns"

	fidlnet "fidl/fuchsia/net"

	"golang.org/x/net/dns/dnsmessage"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

func TestLookupHostname(t *testing.T) {
	ns := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			arp.NewProtocol(),
			ipv4.NewProtocol(),
			ipv6.NewProtocol(),
		},
	})
	dnsClient := dns.NewClient(ns)
	ni := nameLookupImpl{dnsClient: dnsClient}

	var v4loopback fidlnet.IpAddress
	v4loopback.SetIpv4(fidlnet.Ipv4Address{
		Addr: ipv4LoopbackBytes,
	})
	host, err := ni.LookupHostname(v4loopback)
	AssertNoError(t, err)
	switch typ := host.Which(); typ {
	case fidlnet.NameLookupLookupHostnameResultResponse:
		break
	case fidlnet.NameLookupLookupHostnameResultErr:
		t.Fatalf("LookupHostname(%s) returned error: %s", net.IP(v4loopback.Ipv4.Addr[:]), host.Err)
	default:
		t.Fatalf("LookupHostname(%s) returned unexpected variant %d", net.IP(v4loopback.Ipv4.Addr[:]), typ)
	}
	if host.Response.Hostname != "localhost" {
		t.Errorf("LookupHostname(%s) != localhost (got %s)", net.IP(v4loopback.Ipv4.Addr[:]), host.Response.Hostname)
	}

	var v6loopback fidlnet.IpAddress
	v6loopback.SetIpv6(fidlnet.Ipv6Address{Addr: ipv6LoopbackBytes})
	host, err = ni.LookupHostname(v6loopback)
	AssertNoError(t, err)
	switch typ := host.Which(); typ {
	case fidlnet.NameLookupLookupHostnameResultResponse:
		break
	case fidlnet.NameLookupLookupHostnameResultErr:
		t.Fatalf("LookupHostname(%s) returned error: %s", net.IP(v4loopback.Ipv4.Addr[:]), host.Err)
	default:
		t.Fatalf("LookupHostname(%s) returned unexpected variant %d", net.IP(v4loopback.Ipv4.Addr[:]), typ)
	}
	if host.Response.Hostname != "localhost" {
		t.Errorf("LookupHostname(%s) != localhost (got %s)", net.IP(v6loopback.Ipv6.Addr[:]), host.Response.Hostname)
	}
}

func TestLookupIpLocalhost(t *testing.T) {
	ns := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			arp.NewProtocol(),
			ipv4.NewProtocol(),
			ipv6.NewProtocol(),
		},
	})
	dnsClient := dns.NewClient(ns)
	ni := nameLookupImpl{dnsClient: dnsClient}

	testHostname := "localhost"
	ip, err := ni.LookupIp(testHostname, fidlnet.LookupIpOptionsV4Addrs)
	AssertNoError(t, err)
	switch typ := ip.Which(); typ {
	case fidlnet.NameLookupLookupIpResultResponse:
		break
	case fidlnet.NameLookupLookupIpResultErr:
		t.Fatalf("LookupIp(%#v) returned error: %s", testHostname, ip.Err)
	default:
		t.Fatalf("LookupIp(%#v) returned unexpected variant %d", testHostname, typ)
	}
	if want, got := 1, len(ip.Response.Addr.Ipv4Addrs); got != want {
		t.Fatalf("got len(LookupIp(%#v)) = %d, want = %d", testHostname, got, want)
	}
	if want, got := ipv4LoopbackBytes, ip.Response.Addr.Ipv4Addrs[0].Addr; got != want {
		t.Errorf("got LookupIp(%#v) = %s, want = %s", testHostname, got, want)
	}
}

func TestLookupIp(t *testing.T) {
	ns := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			arp.NewProtocol(),
			ipv4.NewProtocol(),
			ipv6.NewProtocol(),
		},
	})
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
	switch typ := ip.Which(); typ {
	case fidlnet.NameLookupLookupIpResultResponse:
		break
	case fidlnet.NameLookupLookupIpResultErr:
		t.Fatalf("LookupIp(%#v) returned error: %s", testHostname, ip.Err)
	default:
		t.Fatalf("LookupIp(%#v) returned unexpected variant %d", testHostname, typ)
	}
	if want, got := 1, len(ip.Response.Addr.Ipv4Addrs); got != want {
		t.Fatalf("got len(LookupIp(%#v)) = %d, want = %d", testHostname, got, want)
	}
	if want, got := testaddr, ip.Response.Addr.Ipv4Addrs[0].Addr; want != got {
		t.Errorf("got LookupIp(%#v) = %s, want = %s", testHostname, got, want)
	}
}
