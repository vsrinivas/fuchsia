// Copyright 2020 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package dns

import (
	"sync"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip"
)

// expiringDNSServerState is the state for an expiring DNS server.
type expiringDNSServerState struct {
	timer tcpip.CancellableTimer
}

// serversConfig holds DNS resolvers' DNS servers configuration.
type serversConfig struct {
	mu struct {
		sync.RWMutex

		// Default DNS server addresses.
		//
		// defaultServers are assumed to use defaultDNSPort for DNS queries.
		defaultServers []tcpip.Address

		// References to slices of DNS servers configured at runtime.
		//
		// Unlike expiringServers, these servers do not have a set lifetime; they
		// are valid forever until updated.
		//
		// runtimeServers are assumed to use defaultDNSPort for DNS queries.
		runtimeServers []*[]tcpip.Address

		// DNS server and associated port configured at runtime that may expire
		// after some lifetime.
		expiringServers map[tcpip.FullAddress]expiringDNSServerState

		// A cache of the available DNS server addresses and associated port that
		// may be used until invalidation.
		//
		// Must be cleared when defaultServers, runtimeServers or expiringServers
		// gets updated.
		serversCache []tcpip.FullAddress
	}
}

func makeServersConfig() serversConfig {
	var d serversConfig
	d.mu.expiringServers = make(map[tcpip.FullAddress]expiringDNSServerState)
	return d
}

// GetServersCache returns a list of tcpip.FullAddress to DNS servers.
//
// The expiring servers will be at the front of the list, followed by
// the runtime and default servers. The list will be deduplicated.
func (d *serversConfig) GetServersCache() []tcpip.FullAddress {
	// If we already have a populated cache, return it.
	d.mu.RLock()
	cache := d.mu.serversCache
	d.mu.RUnlock()
	if cache != nil {
		return cache
	}

	// At this point the cache may need to be generated.

	d.mu.Lock()
	defer d.mu.Unlock()

	// We check if the cache is populated again so that the read lock is leveraged
	// above. We need to check d.mu.serversCache after taking the Write
	// lock to avoid duplicate work when multiple concurrent calls to
	// GetServersCache are made.
	//
	// Without this check, the following can occur (T1 and T2 are goroutines
	// that are trying to get the servers cache):
	//   T1: Take RLock, Drop RLock, cache == nil
	//   T2: Take RLock  Drop RLock, cache == nil
	//   T1: Take WLock
	//   T2: Attempt to take WLock, block
	//   T1: Generate cache, drop WLock
	//   T2: Obtain WLock, generate cache, drop WLock
	//
	// This example can be expanded to many more goroutines like T2.
	//
	// Here we can see that T2 unnessessarily regenerates the cache after T1. By
	// checking if the servers cache is populated after obtaining the WLock, this
	// can be avoided.
	if cache := d.mu.serversCache; cache != nil {
		return cache
	}

	have := make(map[tcpip.FullAddress]struct{})

	for s := range d.mu.expiringServers {
		// We don't check if s is already in have since d.mu.expiringServers
		// is a map - we will not see the same key twice.
		have[s] = struct{}{}
		cache = append(cache, s)
	}

	for _, serverLists := range [][]*[]tcpip.Address{d.mu.runtimeServers, {&d.mu.defaultServers}} {
		for _, serverList := range serverLists {
			for _, server := range *serverList {
				s := tcpip.FullAddress{Addr: server, Port: defaultDNSPort}
				if _, ok := have[s]; ok {
					// cache already has s in it, do not duplicate it.
					continue
				}

				have[s] = struct{}{}
				cache = append(cache, s)
			}
		}
	}

	d.mu.serversCache = cache
	return cache
}

func (d *serversConfig) GetDefaultServers() []tcpip.Address {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return append([]tcpip.Address(nil), d.mu.defaultServers...)
}

// SetDefaultServers sets the default list of nameservers to query.
// This usually comes from a system-wide configuration file.
// Servers are checked sequentially, in order.
// Takes ownership of the passed-in slice of addrs.
func (d *serversConfig) SetDefaultServers(servers []tcpip.Address) {
	d.mu.Lock()
	defer d.mu.Unlock()

	// Clear the cache of DNS servers.
	d.mu.serversCache = nil
	d.mu.defaultServers = servers
}

// SetRuntimeServers sets the list of lists of runtime servers to query (e.g.
// collected from DHCP responses).  Servers are checked sequentially, in order.
// Takes ownership of the passed-in list of runtimeServerRefs.
//
// It's possible to introduce aliasing issues if a slice pointer passed here is
// obviated later but SetRuntimeServers isn't called again.
//
// E.g., if one of the network interface structs containing a slice pointer is
// deleted, SetRuntimeServers should be called again with an updated list of
// runtime server refs.
func (d *serversConfig) SetRuntimeServers(runtimeServerRefs []*[]tcpip.Address) {
	d.mu.Lock()
	defer d.mu.Unlock()

	// Clear the cache of DNS servers.
	d.mu.serversCache = nil
	d.mu.runtimeServers = runtimeServerRefs
}

// UpdateExpiringServers updates the list of expiring servers to query.
//
// If a server already known by c, its lifetime will be refreshed. If a server
// is not known and has a non-zero lifetime, it will be stored and set to
// expire after lifetime.
//
// A lifetime value of less than 0 indicates that servers are not to expire
// (they will become valid forever until another update refreshes the lifetime).
func (d *serversConfig) UpdateExpiringServers(servers []tcpip.FullAddress, lifetime time.Duration) {
	d.mu.Lock()
	defer d.mu.Unlock()

	for _, s := range servers {
		// If s.Port is 0, then assume the default DNS port.
		if s.Port == 0 {
			s.Port = defaultDNSPort
		}

		state, ok := d.mu.expiringServers[s]
		if lifetime != 0 {
			if !ok {
				// Clear the cache of DNS servers since we add a new server.
				d.mu.serversCache = nil

				// We do not yet have the server and it has a non-zero lifetime.
				s := s
				state = expiringDNSServerState{
					timer: tcpip.MakeCancellableTimer(&d.mu, func() {
						// Clear the cache of DNS servers.
						d.mu.serversCache = nil
						delete(d.mu.expiringServers, s)
					}),
				}
			}

			// Refresh s's lifetime.
			state.timer.StopLocked()
			if lifetime > 0 {
				// s is valid for a finite lifetime.
				state.timer.Reset(lifetime)
			}

			d.mu.expiringServers[s] = state
		} else if ok {
			// Clear the cache of DNS servers since we remove a server.
			d.mu.serversCache = nil

			// We have the server and it is no longer to be used.
			state.timer.StopLocked()
			delete(d.mu.expiringServers, s)
		}
	}
}

// RemoveAllServersWithNIC removes all servers associated with the specified
// NIC.
//
// If a NIC is not specified (nicID == 0), then RemoveAllServersWithNIC does
// nothing.
func (d *serversConfig) RemoveAllServersWithNIC(nicID tcpip.NICID) {
	if nicID == 0 {
		return
	}

	d.mu.Lock()
	defer d.mu.Unlock()

	// Clear the cache of DNS servers.
	d.mu.serversCache = nil

	for k := range d.mu.expiringServers {
		if k.NIC == nicID {
			delete(d.mu.expiringServers, k)
		}
	}
}
