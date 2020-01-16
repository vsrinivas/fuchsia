// Copyright 2020 The Netstack Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package dns

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"golang.org/x/net/dns/dnsmessage"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/link/channel"
	"gvisor.dev/gvisor/pkg/tcpip/network/arp"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
)

const (
	incrementalTimeout    = 100 * time.Millisecond
	shortLifetime         = 1 * time.Nanosecond
	shortLifetimeTimeout  = 1 * time.Second
	middleLifetime        = 1 * time.Second
	middleLifetimeTimeout = 2 * time.Second
	longLifetime          = 1 * time.Hour
)

var (
	addr1 = tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
		Port: defaultDNSPort,
	}
	// Address is the same as addr1, but differnt port.
	addr2 = tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
		Port: defaultDNSPort + 1,
	}
	addr3 = tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02",
		Port: defaultDNSPort + 2,
	}
	// Should assume default port of 53.
	addr4 = tcpip.FullAddress{
		Addr: "\xfe\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03",
		NIC:  5,
	}
	addr5 = tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05",
		Port: defaultDNSPort,
	}
	addr6 = tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x06",
		Port: defaultDNSPort,
	}
	addr7 = tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x07",
		Port: defaultDNSPort,
	}
	addr8 = tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x08",
		Port: defaultDNSPort,
	}
	addr9 = tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x09",
		Port: defaultDNSPort,
	}
	addr10 = tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a",
		Port: defaultDNSPort,
	}
)

func containsFullAddress(list []tcpip.FullAddress, item tcpip.FullAddress) bool {
	for _, i := range list {
		if i == item {
			return true
		}
	}

	return false
}

func containsAddress(list []tcpip.Address, item tcpip.Address) bool {
	for _, i := range list {
		if i == item {
			return true
		}
	}

	return false
}

func TestGetServersCacheNoDuplicates(t *testing.T) {
	addr3 := addr3
	addr3.Port = defaultDNSPort
	addr4WithPort := addr4
	addr4WithPort.Port = defaultDNSPort

	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2, addr2, addr3, addr4, addr8}, longLifetime)
	runtimeServers1 := []tcpip.Address{addr5.Addr, addr5.Addr, addr6.Addr, addr7.Addr}
	runtimeServers2 := []tcpip.Address{addr6.Addr, addr7.Addr, addr8.Addr, addr9.Addr}
	c.SetRuntimeServers([]*[]tcpip.Address{&runtimeServers1, &runtimeServers2})
	c.SetDefaultServers([]tcpip.Address{addr3.Addr, addr9.Addr, addr10.Addr, addr10.Addr})
	servers := c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !containsFullAddress(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if !containsFullAddress(servers, addr4WithPort) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr4WithPort, servers)
	}
	if !containsFullAddress(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !containsFullAddress(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if !containsFullAddress(servers, addr7) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr7, servers)
	}
	if !containsFullAddress(servers, addr8) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr8, servers)
	}
	if !containsFullAddress(servers, addr9) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr9, servers)
	}
	if !containsFullAddress(servers, addr10) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr10, servers)
	}
	if l := len(servers); l != 10 {
		t.Errorf("got len(servers) = %d, want = 10; servers = %+v", l, servers)
	}
}

func TestGetServersCacheOrdering(t *testing.T) {
	addr4WithPort := addr4
	addr4WithPort.Port = defaultDNSPort

	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2, addr3, addr4}, longLifetime)
	runtimeServers1 := []tcpip.Address{addr5.Addr, addr6.Addr}
	runtimeServers2 := []tcpip.Address{addr7.Addr, addr8.Addr}
	c.SetRuntimeServers([]*[]tcpip.Address{&runtimeServers1, &runtimeServers2})
	c.SetDefaultServers([]tcpip.Address{addr9.Addr, addr10.Addr})
	servers := c.GetServersCache()
	expiringServers := servers[:4]
	runtimeServers := servers[4:8]
	defaultServers := servers[8:]
	if !containsFullAddress(expiringServers, addr1) {
		t.Errorf("expected %+v to be in the expiring server cache, got = %+v", addr1, expiringServers)
	}
	if !containsFullAddress(expiringServers, addr2) {
		t.Errorf("expected %+v to be in the expiring server cache, got = %+v", addr2, expiringServers)
	}
	if !containsFullAddress(expiringServers, addr3) {
		t.Errorf("expected %+v to be in the expiring server cache, got = %+v", addr3, expiringServers)
	}
	if !containsFullAddress(expiringServers, addr4WithPort) {
		t.Errorf("expected %+v to be in the expiring server cache, got = %+v", addr4WithPort, expiringServers)
	}
	if !containsFullAddress(runtimeServers, addr5) {
		t.Errorf("expected %+v to be in the runtime server cache, got = %+v", addr5, runtimeServers)
	}
	if !containsFullAddress(runtimeServers, addr6) {
		t.Errorf("expected %+v to be in the runtime server cache, got = %+v", addr6, runtimeServers)
	}
	if !containsFullAddress(runtimeServers, addr7) {
		t.Errorf("expected %+v to be in the runtime server cache, got = %+v", addr7, runtimeServers)
	}
	if !containsFullAddress(runtimeServers, addr8) {
		t.Errorf("expected %+v to be in the runtime server cache, got = %+v", addr8, runtimeServers)
	}
	if !containsFullAddress(defaultServers, addr9) {
		t.Errorf("expected %+v to be in the default server cache, got = %+v", addr9, defaultServers)
	}
	if !containsFullAddress(defaultServers, addr10) {
		t.Errorf("expected %+v to be in the default server cache, got = %+v", addr10, defaultServers)
	}
	if l := len(servers); l != 10 {
		t.Errorf("got len(servers) = %d, want = 10; servers = %+v", l, servers)
	}
}

func TestRemoveAllServersWithNIC(t *testing.T) {
	addr3 := addr3
	addr3.NIC = addr4.NIC
	addr4WithPort := addr4
	addr4WithPort.Port = defaultDNSPort

	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	expectAllAddresses := func() {
		t.Helper()

		servers := c.GetServersCache()
		if !containsFullAddress(servers, addr1) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
		}
		if !containsFullAddress(servers, addr2) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
		}
		if !containsFullAddress(servers, addr3) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
		}
		if !containsFullAddress(servers, addr4WithPort) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr4WithPort, servers)
		}
		if !containsFullAddress(servers, addr5) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
		}
		if !containsFullAddress(servers, addr6) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
		}
		if !containsFullAddress(servers, addr7) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr7, servers)
		}
		if !containsFullAddress(servers, addr8) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr8, servers)
		}
		if !containsFullAddress(servers, addr9) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr9, servers)
		}
		if !containsFullAddress(servers, addr10) {
			t.Errorf("expected %+v to be in the server cache, got = %+v", addr10, servers)
		}
		if l := len(servers); l != 10 {
			t.Errorf("got len(servers) = %d, want = 10; servers = %+v", l, servers)
		}

		if t.Failed() {
			t.FailNow()
		}
	}

	c.SetDefaultServers([]tcpip.Address{addr5.Addr, addr6.Addr})
	runtimeServers1 := []tcpip.Address{addr7.Addr, addr8.Addr}
	runtimeServers2 := []tcpip.Address{addr9.Addr, addr10.Addr}
	c.SetRuntimeServers([]*[]tcpip.Address{&runtimeServers1, &runtimeServers2})
	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2, addr3, addr4}, longLifetime)
	expectAllAddresses()

	// Should do nothing since a NIC is not specified.
	c.RemoveAllServersWithNIC(0)
	expectAllAddresses()

	// Should do nothing since none of the addresses are associated with a NIC
	// with ID 255.
	c.RemoveAllServersWithNIC(255)
	expectAllAddresses()

	// Should remove addr3 and addr4.
	c.RemoveAllServersWithNIC(addr4.NIC)
	servers := c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !containsFullAddress(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !containsFullAddress(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if !containsFullAddress(servers, addr7) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr7, servers)
	}
	if !containsFullAddress(servers, addr8) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr8, servers)
	}
	if !containsFullAddress(servers, addr9) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr9, servers)
	}
	if !containsFullAddress(servers, addr10) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr10, servers)
	}
	if l := len(servers); l != 8 {
		t.Errorf("got len(servers) = %d, want = 8; servers = %+v", l, servers)
	}
}

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
	e := channel.New(0, 1280, "\x02\x03\x04\x05\x06\x07")
	if err := s.CreateNIC(nicID, e); err != nil {
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

func TestGetServersCache(t *testing.T) {
	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	addr3 := addr3
	addr3.NIC = addr4.NIC
	addr4WithPort := addr4
	addr4WithPort.Port = defaultDNSPort

	c.SetDefaultServers([]tcpip.Address{addr5.Addr, addr6.Addr})
	servers := c.GetServersCache()
	if !containsFullAddress(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !containsFullAddress(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if l := len(servers); l != 2 {
		t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	runtimeServers1 := []tcpip.Address{addr7.Addr, addr8.Addr}
	runtimeServers2 := []tcpip.Address{addr9.Addr, addr10.Addr}
	c.SetRuntimeServers([]*[]tcpip.Address{&runtimeServers1, &runtimeServers2})
	servers = c.GetServersCache()
	if !containsFullAddress(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !containsFullAddress(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if !containsFullAddress(servers, addr7) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr7, servers)
	}
	if !containsFullAddress(servers, addr8) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr8, servers)
	}
	if !containsFullAddress(servers, addr9) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr9, servers)
	}
	if !containsFullAddress(servers, addr10) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr10, servers)
	}
	if l := len(servers); l != 6 {
		t.Errorf("got len(servers) = %d, want = 6; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2, addr3, addr4}, longLifetime)
	servers = c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !containsFullAddress(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if !containsFullAddress(servers, addr4WithPort) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr4WithPort, servers)
	}
	if !containsFullAddress(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !containsFullAddress(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if !containsFullAddress(servers, addr7) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr7, servers)
	}
	if !containsFullAddress(servers, addr8) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr8, servers)
	}
	if !containsFullAddress(servers, addr9) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr9, servers)
	}
	if !containsFullAddress(servers, addr10) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr10, servers)
	}
	if l := len(servers); l != 10 {
		t.Errorf("got len(servers) = %d, want = 10; servers = %+v", l, servers)
	}

	// Should get the same results since there were no updates.
	if diff := cmp.Diff(servers, c.GetServersCache()); diff != "" {
		t.Errorf("c.GetServersCache() mismatch (-want +got):\n%s", diff)
	}

	if t.Failed() {
		t.FailNow()
	}

	c.SetRuntimeServers(nil)
	servers = c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !containsFullAddress(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if !containsFullAddress(servers, addr4WithPort) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr4WithPort, servers)
	}
	if !containsFullAddress(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !containsFullAddress(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if l := len(servers); l != 6 {
		t.Errorf("got len(servers) = %d, want = 6; servers = %+v", l, servers)
	}

	// Should remove addr3 and addr4.
	c.RemoveAllServersWithNIC(addr4.NIC)
	servers = c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !containsFullAddress(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !containsFullAddress(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if l := len(servers); l != 4 {
		t.Errorf("got len(servers) = %d, want = 4; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	c.SetDefaultServers(nil)
	servers = c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if l := len(servers); l != 2 {
		t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2}, 0)
	servers = c.GetServersCache()
	if l := len(servers); l != 0 {
		t.Errorf("got len(servers) = %d, want = 0; servers = %+v", l, servers)
	}
}

func TestExpiringServersDefaultDNSPort(t *testing.T) {
	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	addr4WithPort := addr4
	addr4WithPort.Port = defaultDNSPort

	c.UpdateExpiringServers([]tcpip.FullAddress{addr4}, longLifetime)
	servers := c.GetServersCache()
	if !containsFullAddress(servers, addr4WithPort) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr4WithPort, servers)
	}
	if l := len(servers); l != 1 {
		t.Errorf("got len(servers) = %d, want = 1; servers = %+v", l, servers)
	}
}

func TestExpiringServersUpdateWithDuplicates(t *testing.T) {
	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr1, addr1}, longLifetime)
	servers := c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if l := len(servers); l != 1 {
		t.Errorf("got len(servers) = %d, want = 1; servers = %+v", l, servers)
	}
}

func TestExpiringServersAddAndUpdate(t *testing.T) {
	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2}, longLifetime)
	servers := c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if l := len(servers); l != 2 {
		t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	// Refresh addr1 and addr2, add addr3.
	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr3, addr2}, longLifetime)
	servers = c.GetServersCache()
	if !containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !containsFullAddress(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if l := len(servers); l != 3 {
		t.Errorf("got len(servers) = %d, want = 3; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	// Lifetime of 0 should remove servers if they exist.
	c.UpdateExpiringServers([]tcpip.FullAddress{addr4, addr1}, 0)
	servers = c.GetServersCache()
	if containsFullAddress(servers, addr1) {
		t.Errorf("expected %+v to not be in the server cache, got = %+v", addr1, servers)
	}
	if containsFullAddress(servers, addr4) {
		t.Errorf("expected %+v to not be in the server cache, got = %+v", addr4, servers)
	}
	if !containsFullAddress(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !containsFullAddress(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if l := len(servers); l != 2 {
		t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}
}

func TestExpiringServersExpireImmediatelyTimer(t *testing.T) {
	t.Parallel()

	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2}, shortLifetime)
	for elapsedTime := time.Duration(0); elapsedTime <= shortLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers := c.GetServersCache()
		if l := len(servers); l != 0 {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Fatalf("got len(servers) = %d, want = 0; servers = %+v", l, servers)
		}

		break
	}
}

func TestExpiringServersExpireAfterUpdate(t *testing.T) {
	t.Parallel()

	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2}, longLifetime)
	servers := c.GetServersCache()
	if l := len(servers); l != 2 {
		t.Fatalf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	// addr2 and addr3 should expire, but addr1 should stay.
	c.UpdateExpiringServers([]tcpip.FullAddress{addr2, addr3}, shortLifetime)
	for elapsedTime := time.Duration(0); elapsedTime <= shortLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers = c.GetServersCache()
		if !containsFullAddress(servers, addr1) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
		}
		if containsFullAddress(servers, addr2) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr3, servers)
		}
		if containsFullAddress(servers, addr3) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr3, servers)
		}
		if l := len(servers); l != 1 {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("got len(servers) = %d, want = 1; servers = %+v", l, servers)
		}

		break
	}
}

func TestExpiringServersInfiniteLifetime(t *testing.T) {
	t.Parallel()

	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2}, middleLifetime)
	servers := c.GetServersCache()
	if l := len(servers); l != 2 {
		t.Fatalf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	// addr1 should expire, but addr2 and addr3 should be valid forever.
	c.UpdateExpiringServers([]tcpip.FullAddress{addr2, addr3}, -1)
	for elapsedTime := time.Duration(0); elapsedTime < middleLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers = c.GetServersCache()
		if containsFullAddress(servers, addr1) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr2, servers)
		}
		if !containsFullAddress(servers, addr2) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
		}
		if !containsFullAddress(servers, addr3) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
		}
		if l := len(servers); l != 2 {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
		}

		break
	}

	if t.Failed() {
		t.FailNow()
	}

	c.UpdateExpiringServers([]tcpip.FullAddress{addr2, addr3}, middleLifetime)
	for elapsedTime := time.Duration(0); elapsedTime <= middleLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers = c.GetServersCache()
		if containsFullAddress(servers, addr1) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr2, servers)
		}
		if containsFullAddress(servers, addr2) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr3, servers)
		}
		if containsFullAddress(servers, addr3) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr3, servers)
		}
		if l := len(servers); l != 0 {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("got len(servers) = %d, want = 0; servers = %+v", l, servers)
		}

		break
	}
}
