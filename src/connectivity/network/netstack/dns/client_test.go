// Copyright 2020 The Netstack Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package dns

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"golang.org/x/net/dns/dnsmessage"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/link/loopback"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

func TestResolver(t *testing.T) {
	const nicID = 1
	const nicIPv4Addr = tcpip.Address("\x01\x02\x03\x04")
	const nicIPv6Addr = tcpip.Address("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10")
	exampleIPv4Addr1Bytes := [4]byte{192, 168, 0, 1}
	exampleIPv4Addr2Bytes := [4]byte{192, 168, 0, 2}
	fooExampleIPv4Addr1Bytes := [4]byte{192, 168, 0, 3}
	fooExampleIPv4Addr2Bytes := [4]byte{192, 168, 0, 4}
	exampleIPv6Addr1Bytes := [16]byte{192, 168, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
	exampleIPv6Addr2Bytes := [16]byte{192, 168, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}
	fooExampleIPv6Addr1Bytes := [16]byte{192, 168, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3}
	fooExampleIPv6Addr2Bytes := [16]byte{192, 168, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4}
	exampleIPv4Addr1 := tcpip.Address(exampleIPv4Addr1Bytes[:])
	exampleIPv4Addr2 := tcpip.Address(exampleIPv4Addr2Bytes[:])
	fooExampleIPv4Addr1 := tcpip.Address(fooExampleIPv4Addr1Bytes[:])
	fooExampleIPv4Addr2 := tcpip.Address(fooExampleIPv4Addr2Bytes[:])
	exampleIPv6Addr1 := tcpip.Address(exampleIPv6Addr1Bytes[:])
	exampleIPv6Addr2 := tcpip.Address(exampleIPv6Addr2Bytes[:])
	fooExampleIPv6Addr1 := tcpip.Address(fooExampleIPv6Addr1Bytes[:])
	fooExampleIPv6Addr2 := tcpip.Address(fooExampleIPv6Addr2Bytes[:])
	fakeIPv4AddrBytes := [4]byte{1, 2, 3, 4}
	// A simple resolver that returns 1.2.3.4 for all A record questions.
	fakeResolver := func(question dnsmessage.Question) (dnsmessage.Name, []dnsmessage.Resource, dnsmessage.Message, error) {
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
						A: fakeIPv4AddrBytes,
					},
				},
			}
		}

		return question.Name, r.Answers, r, nil
	}
	fakeResolverResponse := []tcpip.Address{tcpip.Address(fakeIPv4AddrBytes[:])}

	// We need a Stack because the default resolver tries to find a route to the
	// servers to make sure a route exists. No packets are sent.
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocol{
			arp.NewProtocol(),
			ipv4.NewProtocol(),
			ipv6.NewProtocol(),
		},
		TransportProtocols: []stack.TransportProtocol{
			udp.NewProtocol(),
		},
		HandleLocal: true,
	})
	if err := s.CreateNIC(nicID, loopback.New()); err != nil {
		t.Fatalf("s.CreateNIC(%d, _): %s", nicID, err)
	}
	if err := s.AddAddress(nicID, ipv4.ProtocolNumber, nicIPv4Addr); err != nil {
		t.Fatalf("s.AddAddress(%d, %d, %s): %s", nicID, ipv4.ProtocolNumber, nicIPv4Addr, err)
	}
	if err := s.AddAddress(nicID, ipv6.ProtocolNumber, nicIPv6Addr); err != nil {
		t.Fatalf("s.AddAddress(%d, %d, %s): %s", nicID, ipv6.ProtocolNumber, nicIPv6Addr, err)
	}

	c := NewClient(s)

	// Add some entries to the cache that will be returned when testing the
	// default resolver.
	//
	// We need to add both A and AAAA records to the cache for each domain name
	// to make sure we do not try to query a DNS server through s.
	c.cache.insertAll([]dnsmessage.Resource{
		makeTypeAResource(example, 5, exampleIPv4Addr1Bytes),
		makeTypeAResource(example, 5, exampleIPv4Addr2Bytes),
		makeTypeAResource(fooExample, 5, fooExampleIPv4Addr1Bytes),
		makeTypeAResource(fooExample, 5, fooExampleIPv4Addr2Bytes),
		makeTypeAAAAResource(example, 5, exampleIPv6Addr1Bytes),
		makeTypeAAAAResource(example, 5, exampleIPv6Addr2Bytes),
		makeTypeAAAAResource(fooExample, 5, fooExampleIPv6Addr1Bytes),
		makeTypeAAAAResource(fooExample, 5, fooExampleIPv6Addr2Bytes),
	})

	// We check the default resolver by making sure the entries we populated the
	// cache with is returned.
	checkDefaultResolver := func() {
		t.Helper()

		if addrs, err := c.LookupIP(example); err != nil {
			t.Fatalf("c.LookupIP(%q): %s", example, err)
		} else {
			if !containsAddress(addrs, exampleIPv4Addr1) {
				t.Errorf("expected %s to bein the list of addresses for %s; got = %s", exampleIPv4Addr1, example, addrs)
			}
			if !containsAddress(addrs, exampleIPv4Addr2) {
				t.Errorf("expected %s to bein the list of addresses for %s; got = %s", exampleIPv4Addr2, example, addrs)
			}
			if !containsAddress(addrs, exampleIPv6Addr1) {
				t.Errorf("expected %s to bein the list of addresses for %s; got = %s", exampleIPv6Addr1, example, addrs)
			}
			if !containsAddress(addrs, exampleIPv6Addr2) {
				t.Errorf("expected %s to bein the list of addresses for %s; got = %s", exampleIPv6Addr2, example, addrs)
			}
			if l := len(addrs); l != 4 {
				t.Errorf("got len(addrs) = %d, want = 4; addrs = %s", l, addrs)
			}
		}
		if addrs, err := c.LookupIP(fooExample); err != nil {
			t.Fatalf("c.LookupIP(%q): %s", fooExample, err)
		} else {
			if !containsAddress(addrs, fooExampleIPv4Addr1) {
				t.Errorf("expected %s to bein the list of addresses for %s; got = %s", fooExampleIPv4Addr1, fooExample, addrs)
			}
			if !containsAddress(addrs, fooExampleIPv4Addr2) {
				t.Errorf("expected %s to bein the list of addresses for %s; got = %s", fooExampleIPv4Addr2, fooExample, addrs)
			}
			if !containsAddress(addrs, fooExampleIPv6Addr1) {
				t.Errorf("expected %s to bein the list of addresses for %s; got = %s", fooExampleIPv6Addr1, fooExample, addrs)
			}
			if !containsAddress(addrs, fooExampleIPv6Addr2) {
				t.Errorf("expected %s to bein the list of addresses for %s; got = %s", fooExampleIPv6Addr2, fooExample, addrs)
			}
			if l := len(addrs); l != 4 {
				t.Errorf("got len(addrs) = %d, want = 4; addrs = %s", l, addrs)
			}
		}
	}

	// c should be initialized with the default resolver.
	checkDefaultResolver()

	// Update c to use fakeResolver as its resolver.
	c.SetResolver(fakeResolver)
	if addrs, err := c.LookupIP(example); err != nil {
		t.Fatalf("c.LookupIP(%q): %s", example, err)
	} else {
		if diff := cmp.Diff(fakeResolverResponse, addrs); diff != "" {
			t.Errorf("domain name addresses mismatch (-want +got):\n%s", diff)
		}
	}
	if addrs, err := c.LookupIP(fooExample); err != nil {
		t.Fatalf("c.LookupIP(%q): %s", fooExample, err)
	} else {
		if diff := cmp.Diff(fakeResolverResponse, addrs); diff != "" {
			t.Errorf("domain name addresses mismatch (-want +got):\n%s", diff)
		}
	}

	// A nil Resolver should update c's resolver to the default resolver.
	c.SetResolver(nil)
	checkDefaultResolver()
}
