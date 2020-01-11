// Copyright 2020 The Netstack Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package dns

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"gvisor.dev/gvisor/pkg/tcpip"
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
)

func contains(list []tcpip.FullAddress, item tcpip.FullAddress) bool {
	for _, i := range list {
		if i == item {
			return true
		}
	}

	return false
}

func TestGetServersCache(t *testing.T) {
	// We set stack.Stack to nil since thats not what we are testing here.
	c := NewClient(nil)

	addr5 := tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05",
		Port: defaultDNSPort,
	}
	addr6 := tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x06",
		Port: defaultDNSPort,
	}
	addr7 := tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x07",
		Port: defaultDNSPort,
	}
	addr8 := tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x08",
		Port: defaultDNSPort,
	}
	addr9 := tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x09",
		Port: defaultDNSPort,
	}
	addr10 := tcpip.FullAddress{
		Addr: "\x0a\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a",
		Port: defaultDNSPort,
	}

	c.SetDefaultServers([]tcpip.Address{addr5.Addr, addr6.Addr})
	servers := c.config.getServersCache()
	if !contains(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !contains(servers, addr6) {
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
	servers = c.config.getServersCache()
	if !contains(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !contains(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if !contains(servers, addr7) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr7, servers)
	}
	if !contains(servers, addr8) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr8, servers)
	}
	if !contains(servers, addr9) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr9, servers)
	}
	if !contains(servers, addr10) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr10, servers)
	}
	if l := len(servers); l != 6 {
		t.Errorf("got len(servers) = %d, want = 6; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2, addr3}, longLifetime)
	servers = c.config.getServersCache()
	if !contains(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !contains(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !contains(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if !contains(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !contains(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if !contains(servers, addr7) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr7, servers)
	}
	if !contains(servers, addr8) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr8, servers)
	}
	if !contains(servers, addr9) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr9, servers)
	}
	if !contains(servers, addr10) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr10, servers)
	}
	if l := len(servers); l != 9 {
		t.Errorf("got len(servers) = %d, want = 9; servers = %+v", l, servers)
	}

	// Should get the same results since there were no updates.
	if diff := cmp.Diff(servers, c.config.getServersCache()); diff != "" {
		t.Errorf("c.config.getServersCache() mismatch (-want +got):\n%s", diff)
	}

	if t.Failed() {
		t.FailNow()
	}

	c.SetRuntimeServers(nil)
	servers = c.config.getServersCache()
	if !contains(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !contains(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !contains(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if !contains(servers, addr5) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr5, servers)
	}
	if !contains(servers, addr6) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr6, servers)
	}
	if l := len(servers); l != 5 {
		t.Errorf("got len(servers) = %d, want = 5; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	c.SetDefaultServers(nil)
	servers = c.config.getServersCache()
	if !contains(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !contains(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !contains(servers, addr3) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
	}
	if l := len(servers); l != 3 {
		t.Errorf("got len(servers) = %d, want = 3; servers = %+v", l, servers)
	}

	if t.Failed() {
		t.FailNow()
	}

	c.UpdateExpiringServers([]tcpip.FullAddress{addr1, addr2, addr3}, 0)
	servers = c.config.getServersCache()
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
	servers := c.config.getServersCache()
	if !contains(servers, addr4WithPort) {
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
	servers := c.config.getServersCache()
	if !contains(servers, addr1) {
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
	servers := c.config.getServersCache()
	if !contains(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !contains(servers, addr2) {
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
	servers = c.config.getServersCache()
	if !contains(servers, addr1) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr1, servers)
	}
	if !contains(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !contains(servers, addr3) {
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
	servers = c.config.getServersCache()
	if contains(servers, addr1) {
		t.Errorf("expected %+v to not be in the server cache, got = %+v", addr1, servers)
	}
	if contains(servers, addr4) {
		t.Errorf("expected %+v to not be in the server cache, got = %+v", addr4, servers)
	}
	if !contains(servers, addr2) {
		t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
	}
	if !contains(servers, addr3) {
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
		servers := c.config.getServersCache()
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
	servers := c.config.getServersCache()
	if l := len(servers); l != 2 {
		t.Fatalf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	// addr2 and addr3 should expire, but addr1 should stay.
	c.UpdateExpiringServers([]tcpip.FullAddress{addr2, addr3}, shortLifetime)
	for elapsedTime := time.Duration(0); elapsedTime <= shortLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers = c.config.getServersCache()
		if !contains(servers, addr1) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to be in the server cache, got = %+v", addr2, servers)
		}
		if contains(servers, addr2) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr3, servers)
		}
		if contains(servers, addr3) {
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
	servers := c.config.getServersCache()
	if l := len(servers); l != 2 {
		t.Fatalf("got len(servers) = %d, want = 2; servers = %+v", l, servers)
	}

	// addr1 should expire, but addr2 and addr3 should be valid forever.
	c.UpdateExpiringServers([]tcpip.FullAddress{addr2, addr3}, -1)
	for elapsedTime := time.Duration(0); elapsedTime < middleLifetimeTimeout; elapsedTime += incrementalTimeout {
		time.Sleep(incrementalTimeout)
		servers = c.config.getServersCache()
		if contains(servers, addr1) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr2, servers)
		}
		if !contains(servers, addr2) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to be in the server cache, got = %+v", addr3, servers)
		}
		if !contains(servers, addr3) {
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
		servers = c.config.getServersCache()
		if contains(servers, addr1) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr2, servers)
		}
		if contains(servers, addr2) {
			if elapsedTime < middleLifetimeTimeout {
				continue
			}

			t.Errorf("expected %+v to not be in the server cache, got = %+v", addr3, servers)
		}
		if contains(servers, addr3) {
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
