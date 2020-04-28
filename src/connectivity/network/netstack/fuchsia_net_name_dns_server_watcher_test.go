// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"sync"
	"syscall/zx"
	"testing"
	"time"

	"netstack/dns"
	"netstack/fidlconv"

	"fidl/fuchsia/net"
	"fidl/fuchsia/net/name"

	"gvisor.dev/gvisor/pkg/tcpip"
)

const (
	defaultServerIpv4 tcpip.Address = "\x08\x08\x08\x08"
	altServerIpv4     tcpip.Address = "\x08\x08\x04\x04"
	defaultServerIpv6 tcpip.Address = "\x20\x01\x48\x60\x48\x60\x00\x00\x00\x00\x00\x00\x00\x00\x88\x88"
)

var (
	defaultServerIPv4SocketAddress = fidlconv.ToNetSocketAddress(tcpip.FullAddress{
		NIC:  0,
		Addr: defaultServerIpv4,
		Port: dns.DefaultDNSPort,
	})
	altServerIpv4SocketAddress = fidlconv.ToNetSocketAddress(tcpip.FullAddress{
		NIC:  0,
		Addr: altServerIpv4,
		Port: dns.DefaultDNSPort,
	})
	defaultServerIpv6FullAddress = tcpip.FullAddress{
		NIC:  0,
		Addr: defaultServerIpv6,
		Port: dns.DefaultDNSPort,
	}
	defaultServerIpv6SocketAddress = fidlconv.ToNetSocketAddress(defaultServerIpv6FullAddress)

	staticDnsSource = name.DnsServerSource{
		I_dnsServerSourceTag: name.DnsServerSourceStaticSource,
	}
)

func newDnsServer(address net.SocketAddress, source name.DnsServerSource, nicid tcpip.NICID) name.DnsServer {
	return name.DnsServer{
		Address:        address,
		AddressPresent: true,
		Source:         source,
		SourcePresent:  true,
	}
}

func createCollection(t *testing.T) *dnsServerWatcherCollection {
	watcherCollection, err := newDnsServerWatcherCollection(dns.NewClient(nil))
	if err != nil {
		t.Fatalf("failed to create dnsServerWatcherCollection: %s", err)
	}
	watcherCollection.dnsClient.SetOnServersChanged(watcherCollection.NotifyServersChanged)
	return watcherCollection
}

func bindWatcher(t *testing.T, watcherCollection *dnsServerWatcherCollection) *name.DnsServerWatcherWithCtxInterface {
	request, watcher, err := name.NewDnsServerWatcherWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("failed to create DnsServerWatcher channel pair: %s", err)
	}
	if err := watcherCollection.Bind(request); err != nil {
		t.Fatalf("failed to bind watcher: %s", err)
	}
	return watcher
}

func TestDnsWatcherResolvesAndBlocks(t *testing.T) {
	watcherCollection := createCollection(t)
	defer watcherCollection.Close()

	watcherCollection.dnsClient.SetDefaultServers([]tcpip.Address{defaultServerIpv4, defaultServerIpv6})

	watcher := bindWatcher(t, watcherCollection)
	defer watcher.Close()

	servers, err := watcher.WatchServers(context.Background())
	if err != nil {
		t.Fatalf("failed to call WatchServers: %s", err)
	}
	if len(servers) != 2 {
		t.Fatalf("wrong number of servers, expected 2 got %d: %v", len(servers), servers)
	}

	expect := newDnsServer(defaultServerIPv4SocketAddress, staticDnsSource, 0)
	if servers[0] != expect {
		t.Fatalf("incorrect server in first position.\nExpected: %+v\nGot: %+v", expect, servers[0])
	}
	expect = newDnsServer(defaultServerIpv6SocketAddress, staticDnsSource, 0)
	if servers[1] != expect {
		t.Fatalf("incorrect server in first second position.\nExpected: %+v\nGot: %+v", expect, servers[1])
	}

	type watchServersResult struct {
		servers []name.DnsServer
		err     error
	}

	var wg sync.WaitGroup
	wg.Add(1)
	c := make(chan watchServersResult)
	go func() {
		servers, err := watcher.WatchServers(context.Background())
		c <- watchServersResult{
			servers: servers,
			err:     err,
		}
	}()

	select {
	case res := <-c:
		t.Fatalf("WatchServers finished too early with %+v. Should've timed out.", res)
	case <-time.After(50 * time.Millisecond):
	}

	// Clear the list of servers, now we should get something on the channel.
	watcherCollection.dnsClient.SetDefaultServers(nil)

	result := <-c

	if result.err != nil {
		t.Fatalf("WatchServers failed: %s", result.err)
	}

	if len(result.servers) != 0 {
		t.Fatalf("got %+v, expected empty slice", result.servers)
	}
}

func TestDnsWatcherCancelledContext(t *testing.T) {
	watcherCollection := createCollection(t)
	defer watcherCollection.Close()

	watcher := &dnsServerWatcher{
		dnsClient:  watcherCollection.dnsClient,
		dispatcher: watcherCollection.dispatcher,
		channel:    zx.Channel(zx.HANDLE_INVALID),
	}
	watcher.broadcast = &watcherCollection.broadcast

	ctx, cancel := context.WithCancel(context.Background())

	go cancel()

	_, err := watcher.WatchServers(ctx)
	if err == nil {
		t.Fatalf("expected error due to cancelled context")
	}
	watcher.mu.Lock()
	defer watcher.mu.Unlock()
	if !watcher.mu.isDead {
		t.Fatalf("watcher must be marked as dead on context cancellation")
	}
	if watcher.mu.isHanging {
		t.Fatalf("watcher must not be marked as hanging on context cancellation")
	}
}

func TestDnsWatcherDisallowMultiplePending(t *testing.T) {
	t.Skip("Go bindings don't currently support simultaneous calls, test is invalid.")
	watcherCollection := createCollection(t)
	defer watcherCollection.Close()

	watcher := bindWatcher(t, watcherCollection)
	defer watcher.Close()

	var wg sync.WaitGroup
	wg.Add(2)

	watchServers := func() {
		_, err := watcher.WatchServers(context.Background())
		if err == nil {
			t.Errorf("non-nil error watching servers, expected error")
		}
		wg.Done()
	}

	go watchServers()
	go watchServers()

	wg.Wait()
}

func TestDnsWatcherMultipleWatchers(t *testing.T) {
	watcherCollection := createCollection(t)
	defer watcherCollection.Close()

	watcher1 := bindWatcher(t, watcherCollection)
	defer watcher1.Close()

	watcher2 := bindWatcher(t, watcherCollection)
	defer watcher2.Close()

	var wg sync.WaitGroup
	wg.Add(2)

	checkWatcher := func(watcher *name.DnsServerWatcherWithCtxInterface) {
		defer wg.Done()
		servers, err := watcher.WatchServers(context.Background())
		if err != nil {
			t.Errorf("WatchServers failed: %s", err)
			return
		}
		expectedServer := fidlconv.ToNetSocketAddress(tcpip.FullAddress{
			NIC:  0,
			Addr: defaultServerIpv4,
			Port: dns.DefaultDNSPort,
		})
		if len(servers) != 1 {
			t.Errorf("bad server list.\nExpected: %+v\nGot: %+v", []net.SocketAddress{expectedServer}, servers)
			return
		}
		if servers[0].Address != expectedServer {
			t.Errorf("wrong server returned.\nExpected: %+v.\nGot: %+v", expectedServer, servers[0])
		}
	}

	go checkWatcher(watcher1)
	go checkWatcher(watcher2)

	watcherCollection.dnsClient.SetDefaultServers([]tcpip.Address{defaultServerIpv4})

	wg.Wait()
}

func TestDnsWatcherServerListEquality(t *testing.T) {
	addr1 := dns.Server{
		Address: tcpip.FullAddress{
			NIC:  0,
			Addr: defaultServerIpv4,
			Port: dns.DefaultDNSPort,
		},
		Source: staticDnsSource,
	}
	addr2 := dns.Server{
		Address: tcpip.FullAddress{
			NIC:  0,
			Addr: defaultServerIpv6,
			Port: dns.DefaultDNSPort,
		},
		Source: staticDnsSource,
	}
	addr3 := dns.Server{
		Address: tcpip.FullAddress{
			NIC:  0,
			Addr: defaultServerIpv4,
			Port: 8080,
		},
		Source: staticDnsSource,
	}

	testEquality := func(equals bool, l, r []dns.Server) {
		if serverListEquals(l, r) != equals {
			t.Errorf("list comparison failed %+v == %+v should be %v, got %v", l, r, equals, !equals)
		}
	}

	testEquality(true, []dns.Server{addr1, addr2, addr3}, []dns.Server{addr1, addr2, addr3})
	testEquality(true, []dns.Server{}, nil)
	testEquality(false, []dns.Server{addr1, addr2, addr3}, []dns.Server{addr3, addr2, addr1})
	testEquality(false, []dns.Server{addr1}, []dns.Server{addr3, addr2, addr1})
	testEquality(false, []dns.Server{addr1}, []dns.Server{addr2})
}

func TestDnsWatcherDifferentAddressTypes(t *testing.T) {
	watcherCollection := createCollection(t)
	defer watcherCollection.Close()

	watcher := bindWatcher(t, watcherCollection)
	defer watcher.Close()

	watcherCollection.dnsClient.SetDefaultServers([]tcpip.Address{defaultServerIpv4})
	watcherCollection.dnsClient.UpdateDhcpServers(0, &[]tcpip.Address{altServerIpv4})
	watcherCollection.dnsClient.UpdateNdpServers([]tcpip.FullAddress{defaultServerIpv6FullAddress}, -1)

	checkServers := func(expect []net.SocketAddress) {
		servers, err := watcher.WatchServers(context.Background())
		if err != nil {
			t.Fatalf("WatchServers failed: %s", err)
		}
		if len(servers) != len(expect) {
			t.Fatalf("bad WatchServers result, expected %d servers got: %+v", len(expect), servers)
		}
		for i := range expect {
			if servers[i].Address != expect[i] {
				t.Fatalf("bad server at %d.\nExpected: %+v\nGot: %+v", i, expect[i], servers[i])
			}
		}
	}

	checkServers([]net.SocketAddress{defaultServerIpv6SocketAddress, altServerIpv4SocketAddress, defaultServerIPv4SocketAddress})

	// Remove the expiring server and verify result.
	watcherCollection.dnsClient.UpdateNdpServers([]tcpip.FullAddress{defaultServerIpv6FullAddress}, 0)
	checkServers([]net.SocketAddress{altServerIpv4SocketAddress, defaultServerIPv4SocketAddress})

	// Remove DHCP server and verify result.
	watcherCollection.dnsClient.UpdateDhcpServers(0, nil)
	checkServers([]net.SocketAddress{defaultServerIPv4SocketAddress})

	// Remove the last server and verify result.
	watcherCollection.dnsClient.SetDefaultServers(nil)
	checkServers([]net.SocketAddress{})
}
