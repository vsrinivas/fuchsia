// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// DNS client: see RFC 1035.
// Has to be linked into package net for Dial.

// TODO(rsc):
//	Could potentially handle many outstanding lookups faster.
//	Could have a small cache.
//	Random UDP source port (net.Dial should do that for us).
//	Random request IDs.
// TODO(mpcomplete):
//      Cleanup
//      Decide whether we need DNSSEC, EDNS0, reverse DNS or other query types
//      We don't support ipv6 zones. Do we need to?

package dns

import (
	"context"
	"errors"
	"fmt"
	"math/rand"
	"strings"
	"sync"
	"time"

	"golang.org/x/net/dns/dnsmessage"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

// defaultDNSPort is the default port used by DNS servers.
const defaultDNSPort = 53

// Client is a DNS client.
type Client struct {
	stack  *stack.Stack
	config clientConfig
	cache  cacheInfo
}

// A Resolver answers DNS Questions.
type Resolver func(question dnsmessage.Question) (dnsmessage.Name, []dnsmessage.Resource, dnsmessage.Message, error)

// Error represents an error while issuing a DNS query for a hostname.
type Error struct {
	Err           string             // a general error string
	Name          string             // the hostname being queried
	Server        *tcpip.FullAddress // optional DNS server
	CacheNegative bool               // true if this represents a negative response that should be cached (RFC 2308)
}

func (e *Error) Error() string {
	if e.Server != nil {
		return fmt.Sprintf("lookup %s on %+v: %s", e.Name, *e.Server, e.Err)
	}
	return fmt.Sprintf("lookup %s: %s", e.Name, e.Err)
}

// NewClient creates a DNS client with a default resolver.
//
// The default resolver is a Resolver that queries the client's configured DNS
// servers if the DNS question does not already have an answer in the client's
// DNS cache.
func NewClient(s *stack.Stack) *Client {
	c := &Client{
		stack: s,
		config: clientConfig{
			ndots:    100,
			timeout:  3 * time.Second,
			attempts: 3,
			rotate:   true,
		},
		cache: makeCache(),
	}
	c.config.mu.resolver = c.defaultResolver()
	c.config.mu.expiringServers = make(map[tcpip.FullAddress]expiringDNSServerState)
	return c
}

// roundTrip writes the query to and reads the response from the Endpoint.
// The message format is slightly different depending on the transport protocol
// (for TCP, a 2 byte message length is prepended). See RFC 1035.
func roundTrip(ctx context.Context, transport tcpip.TransportProtocolNumber, ep tcpip.Endpoint, wq *waiter.Queue, query *dnsmessage.Message) (dnsmessage.Message, error) {
	b, err := query.Pack()
	if err != nil {
		return dnsmessage.Message{}, err
	}
	if transport == tcp.ProtocolNumber {
		l := len(b)
		b = append([]byte{byte(l >> 8), byte(l)}, b...)
	}

	// Write to endpoint.
	for len(b) > 0 {
		// TODO(tamird): this incorrectly handles short writes.
		n, resCh, err := ep.Write(tcpip.SlicePayload(b), tcpip.WriteOptions{})
		b = b[n:]
		if resCh != nil {
			select {
			case <-resCh:
				continue
			case <-ctx.Done():
				return dnsmessage.Message{}, fmt.Errorf("dns: write: %v (%v)", err, ctx.Err())
			}
		}

		if err != nil {
			return dnsmessage.Message{}, fmt.Errorf("dns: write: %v", err)
		}
	}

	// Read from endpoint.
	b = []byte{}
	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	wq.EventRegister(&waitEntry, waiter.EventIn)
	defer wq.EventUnregister(&waitEntry)
	for {
		v, _, err := ep.Read(nil)
		if err != nil {
			if err == tcpip.ErrClosedForReceive {
				return dnsmessage.Message{}, nil
			}

			if err == tcpip.ErrWouldBlock {
				select {
				case <-notifyCh:
					continue
				case <-ctx.Done():
					return dnsmessage.Message{}, fmt.Errorf("dns: read: %v", tcpip.ErrTimeout)
				}
			}

			return dnsmessage.Message{}, fmt.Errorf("dns: read: %v", err)
		}

		b = append(b, []byte(v)...)

		// Get the contents of the response.
		var bcontents []byte
		switch transport {
		case tcp.ProtocolNumber:
			if len(b) > 2 {
				l := int(b[0])<<8 | int(b[1])
				bcontents = b[2:(l + 2)]
			} else {
				continue
			}
		case udp.ProtocolNumber:
			bcontents = b
		}

		var response dnsmessage.Message
		if err := response.Unpack(bcontents); err != nil {
			// Ignore invalid responses as they may be malicious
			// forgery attempts. Instead continue waiting until
			// timeout. See golang.org/issue/13281.
			continue
		}
		return response, nil
	}
}

// defaultResolver returns a new resolver that queries configured DNS servers
// only if question does not have an answer in c's DNS cache.
func (c *Client) defaultResolver() Resolver {
	return c.cache.newResolverWithFallback(func(question dnsmessage.Question) (dnsmessage.Name, []dnsmessage.Resource, dnsmessage.Message, error) {
		return c.tryOneName(question.Name, question.Type)
	})
}

func (c *Client) connect(ctx context.Context, transport tcpip.TransportProtocolNumber, server tcpip.FullAddress) (tcpip.Endpoint, *waiter.Queue, error) {
	var netproto tcpip.NetworkProtocolNumber
	switch len(server.Addr) {
	case header.IPv4AddressSize:
		netproto = ipv4.ProtocolNumber
	case header.IPv6AddressSize:
		netproto = ipv6.ProtocolNumber
	default:
		return nil, nil, fmt.Errorf("dns: invalid address %s", server.Addr)
	}

	var wq waiter.Queue
	ep, err := c.stack.NewEndpoint(transport, netproto, &wq)
	if err != nil {
		return nil, nil, fmt.Errorf("dns: NewEndpoint(%d, %d, _): %s", transport, netproto, err)
	}

	// Issue connect request and wait for it to complete.
	waitEntry, notifyCh := waiter.NewChannelEntry(nil)
	wq.EventRegister(&waitEntry, waiter.EventOut)
	err = ep.Connect(server)
	defer wq.EventUnregister(&waitEntry)
	if err == tcpip.ErrConnectStarted {
		select {
		case <-notifyCh:
			err = ep.GetSockOpt(tcpip.ErrorOption{})
		case <-ctx.Done():
			err = tcpip.ErrTimeout
		}
	}

	if err != nil {
		return nil, nil, fmt.Errorf("dns: %v", err)
	}

	return ep, &wq, nil
}

// exchange sends a query on the connection and hopes for a response.
func (c *Client) exchange(server tcpip.FullAddress, name dnsmessage.Name, qtype dnsmessage.Type, timeout time.Duration) (dnsmessage.Message, error) {
	query := dnsmessage.Message{
		Header: dnsmessage.Header{
			RecursionDesired: true,
		},
		Questions: []dnsmessage.Question{
			{Name: name, Type: qtype, Class: dnsmessage.ClassINET},
		},
	}

	for _, proto := range []tcpip.TransportProtocolNumber{udp.ProtocolNumber, tcp.ProtocolNumber} {
		response, err := func() (dnsmessage.Message, error) {
			ctx, cancel := context.WithTimeout(context.Background(), timeout)
			defer cancel()

			ep, wq, err := c.connect(ctx, proto, server)
			if err != nil {
				return dnsmessage.Message{}, err
			}
			defer ep.Close()

			query.ID = uint16(rand.Int()) ^ uint16(time.Now().UnixNano())
			return roundTrip(ctx, proto, ep, wq, &query)
		}()
		if err != nil {
			return dnsmessage.Message{}, err
		}
		if response.Truncated { // see RFC 5966
			continue
		}
		return response, nil
	}
	return dnsmessage.Message{}, errors.New("no answer from the DNS server")
}

// Do a lookup for a single name, which must be rooted
// (otherwise answer will not find the answers).
func (c *Client) tryOneName(name dnsmessage.Name, qtype dnsmessage.Type) (dnsmessage.Name, []dnsmessage.Resource, dnsmessage.Message, error) {
	var lastErr error

	servers := c.GetServersCache()

	for i := 0; i < c.config.attempts; i++ {
		for _, server := range servers {
			msg, err := c.exchange(server, name, qtype, c.config.timeout)
			if err != nil {
				lastErr = &Error{
					Err:    err.Error(),
					Name:   name.String(),
					Server: &server,
				}
				continue
			}
			// libresolv continues to the next server when it receives
			// an invalid referral response. See golang.org/issue/15434.
			if msg.RCode == dnsmessage.RCodeSuccess && !msg.Authoritative && !msg.RecursionAvailable && len(msg.Answers) == 0 && len(msg.Additionals) == 0 {
				lastErr = &Error{Err: "lame referral", Name: name.String(), Server: &server}
				continue
			}

			cname, rrs, err := answer(name, server, msg, qtype)
			// If answer errored for rcodes dnsRcodeSuccess or dnsRcodeNameError,
			// it means the response in msg was not useful and trying another
			// server probably won't help. Return now in those cases.
			// TODO: indicate this in a more obvious way, such as a field on Error?
			if err == nil || msg.RCode == dnsmessage.RCodeSuccess || msg.RCode == dnsmessage.RCodeNameError {
				return cname, rrs, msg, err
			}
			lastErr = err
		}
	}

	if lastErr == nil {
		lastErr = &Error{Err: "no DNS servers", Name: name.String()}
	}

	return dnsmessage.Name{}, nil, dnsmessage.Message{}, lastErr
}

// addrRecordList converts and returns a list of IP addresses from DNS
// address records (both A and AAAA). Other record types are ignored.
func addrRecordList(rrs []dnsmessage.Resource) []tcpip.Address {
	addrs := make([]tcpip.Address, 0, 4)
	for _, rr := range rrs {
		switch rr := rr.Body.(type) {
		case *dnsmessage.AResource:
			addrs = append(addrs, tcpip.Address(rr.A[:]))
		case *dnsmessage.AAAAResource:
			addrs = append(addrs, tcpip.Address(rr.AAAA[:]))
		}
	}
	return addrs
}

// expiringDNSServerState is the state for an expiring DNS server.
type expiringDNSServerState struct {
	timer tcpip.CancellableTimer
}

// A clientConfig represents a DNS stub resolver configuration.
type clientConfig struct {
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
		resolver     Resolver // a handler which answers DNS Questions
	}
	search   []string      // rooted suffixes to append to local name
	ndots    int           // number of dots in name to trigger absolute lookup
	timeout  time.Duration // wait before giving up on a query, including retries
	attempts int           // lost packets before giving up on server
	rotate   bool          // round robin among servers
}

// avoidDNS reports whether this is a hostname for which we should not
// use DNS. Currently this includes only .onion, per RFC 7686. See
// golang.org/issue/13705. Does not cover .local names (RFC 6762),
// see golang.org/issue/16739.
func avoidDNS(name string) bool {
	if name == "" {
		return true
	}
	if name[len(name)-1] == '.' {
		name = name[:len(name)-1]
	}
	return strings.HasSuffix(name, ".onion")
}

// nameList returns a list of names for sequential DNS queries.
func (conf *clientConfig) nameList(name string) []string {
	if avoidDNS(name) {
		return nil
	}

	// If name is rooted (trailing dot), try only that name.
	rooted := len(name) > 0 && name[len(name)-1] == '.'
	if rooted {
		return []string{name}
	}

	// hasNdots := count(name, '.') >= conf.ndots
	hasNdots := false
	name += "."

	// Build list of search choices.
	names := make([]string, 0, 1+len(conf.search))
	// If name has enough dots, try unsuffixed first.
	if hasNdots {
		names = append(names, name)
	}
	// Try suffixes.
	for _, suffix := range conf.search {
		names = append(names, name+suffix)
	}
	// Try unsuffixed, if not tried first above.
	if !hasNdots {
		names = append(names, name)
	}
	return names
}

// GetServersCache returns a list of tcpip.FullAddress to DNS servers.
//
// The expiring servers will be at the front of the list, followed by
// the runtime and default servers. The list will be deduplicated.
func (c *Client) GetServersCache() []tcpip.FullAddress {
	// If we already have a populated cache, return it.
	c.config.mu.RLock()
	cache := c.config.mu.serversCache
	c.config.mu.RUnlock()
	if cache != nil {
		return cache
	}

	// At this point the cache may need to be generated.

	c.config.mu.Lock()
	defer c.config.mu.Unlock()

	// We check if the cache is populated again so that the read lock is leveraged
	// above. We need to check c.config.mu.serversCache after taking the Write
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
	if cache := c.config.mu.serversCache; cache != nil {
		return cache
	}

	have := make(map[tcpip.FullAddress]struct{})

	for s := range c.config.mu.expiringServers {
		// We don't check if s is already in have since c.config.mu.expiringServers
		// is a map - we will not see the same key twice.
		have[s] = struct{}{}
		cache = append(cache, s)
	}

	for _, serverLists := range [][]*[]tcpip.Address{c.config.mu.runtimeServers, {&c.config.mu.defaultServers}} {
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

	c.config.mu.serversCache = cache
	return cache
}

func (c *Client) GetDefaultServers() []tcpip.Address {
	c.config.mu.RLock()
	defer c.config.mu.RUnlock()

	return append([]tcpip.Address(nil), c.config.mu.defaultServers...)
}

// SetDefaultServers sets the default list of nameservers to query.
// This usually comes from a system-wide configuration file.
// Servers are checked sequentially, in order.
// Takes ownership of the passed-in slice of addrs.
func (c *Client) SetDefaultServers(servers []tcpip.Address) {
	c.config.mu.Lock()
	defer c.config.mu.Unlock()

	// Clear the cache of DNS servers.
	c.config.mu.serversCache = nil
	c.config.mu.defaultServers = servers
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
func (c *Client) SetRuntimeServers(runtimeServerRefs []*[]tcpip.Address) {
	c.config.mu.Lock()
	defer c.config.mu.Unlock()

	// Clear the cache of DNS servers.
	c.config.mu.serversCache = nil
	c.config.mu.runtimeServers = runtimeServerRefs
}

// UpdateExpiringServers updates the list of expiring servers to query.
//
// If a server already known by c, its lifetime will be refreshed. If a server
// is not known and has a non-zero lifetime, it will be stored and set to
// expire after lifetime.
//
// A lifetime value of less than 0 indicates that servers are not to expire
// (they will become valid forever until another update refreshes the lifetime).
func (c *Client) UpdateExpiringServers(servers []tcpip.FullAddress, lifetime time.Duration) {
	c.config.mu.Lock()
	defer c.config.mu.Unlock()

	for _, s := range servers {
		// If s.Port is 0, then assume the default DNS port.
		if s.Port == 0 {
			s.Port = defaultDNSPort
		}

		state, ok := c.config.mu.expiringServers[s]
		if lifetime != 0 {
			if !ok {
				// Clear the cache of DNS servers since we add a new server.
				c.config.mu.serversCache = nil

				// We do not yet have the server and it has a non-zero lifetime.
				s := s
				state = expiringDNSServerState{
					timer: tcpip.MakeCancellableTimer(&c.config.mu, func() {
						// Clear the cache of DNS servers.
						c.config.mu.serversCache = nil
						delete(c.config.mu.expiringServers, s)
					}),
				}
			}

			// Refresh s's lifetime.
			state.timer.StopLocked()
			if lifetime > 0 {
				// s is valid for a finite lifetime.
				state.timer.Reset(lifetime)
			}

			c.config.mu.expiringServers[s] = state
		} else if ok {
			// Clear the cache of DNS servers since we remove a server.
			c.config.mu.serversCache = nil

			// We have the server and it is no longer to be used.
			state.timer.StopLocked()
			delete(c.config.mu.expiringServers, s)
		}
	}
}

// RemoveAllServersWithNIC removes all servers associated with the specified
// NIC.
//
// If a NIC is not specified (nicID == 0), then RemoveAllServersWithNIC does
// nothing.
func (c *Client) RemoveAllServersWithNIC(nicID tcpip.NICID) {
	if nicID == 0 {
		return
	}

	c.config.mu.Lock()
	defer c.config.mu.Unlock()

	// Clear the cache of DNS servers.
	c.config.mu.serversCache = nil

	for k := range c.config.mu.expiringServers {
		if k.NIC == nicID {
			delete(c.config.mu.expiringServers, k)
		}
	}
}

// SetResolver is used to configure the way c looks up domain names.
//
// If resolver is nil, the default resolver will be used.
//
// SetResolver is useful for tests to mock domain name resolution.
func (c *Client) SetResolver(resolver Resolver) {
	c.config.mu.Lock()
	defer c.config.mu.Unlock()

	if resolver == nil {
		resolver = c.defaultResolver()
	}

	c.config.mu.resolver = resolver
}

// LookupIP returns a list of IP addresses that are registered for the give domain name.
func (c *Client) LookupIP(name string) (addrs []tcpip.Address, err error) {
	if !isDomainName(name) {
		return nil, &Error{Err: "invalid domain name", Name: name}
	}
	c.config.mu.RLock()
	resolver := c.config.mu.resolver
	c.config.mu.RUnlock()
	type racer struct {
		fqdn string
		rrs  []dnsmessage.Resource
		error
	}
	lane := make(chan racer, 1)
	qtypes := [...]dnsmessage.Type{dnsmessage.TypeA, dnsmessage.TypeAAAA}
	var lastErr error
	for _, fqdn := range c.config.nameList(name) {
		for _, qtype := range qtypes {
			name, err := dnsmessage.NewName(fqdn)
			if err != nil {
				continue
			}
			go func(qtype dnsmessage.Type, fqdn string) {
				_, rrs, _, err := resolver(dnsmessage.Question{Name: name, Type: qtype, Class: dnsmessage.ClassINET})
				lane <- racer{fqdn, rrs, err}
			}(qtype, fqdn)
		}
		for range qtypes {
			racer := <-lane
			if racer.error != nil {
				// Prefer error for original name.
				if lastErr == nil || racer.fqdn == name+"." {
					lastErr = racer.error
				}
				continue
			}
			addrs = append(addrs, addrRecordList(racer.rrs)...)
		}
		if len(addrs) > 0 {
			break
		}
	}
	if lastErr, ok := lastErr.(*Error); ok {
		// Show original name passed to lookup, not suffixed one.
		// In general we might have tried many suffixes; showing
		// just one is misleading. See also golang.org/issue/6324.
		lastErr.Name = name
	}
	sortByRFC6724(c, addrs)
	if len(addrs) == 0 && lastErr != nil {
		return nil, lastErr
	}
	return addrs, nil
}

const noSuchHost = "no such host"

// Answer extracts the appropriate answer for a DNS lookup
// for (name, qtype) from the response message msg, which
// is assumed to have come from server.
// It is exported mainly for use by registered helpers.
func answer(name dnsmessage.Name, server tcpip.FullAddress, msg dnsmessage.Message, qtype dnsmessage.Type) (cname dnsmessage.Name, addrs []dnsmessage.Resource, err error) {
	addrs = make([]dnsmessage.Resource, 0, len(msg.Answers))
	if msg.RCode == dnsmessage.RCodeNameError {
		// TODO: There seem to be some cases where we should cache a name error, but not all. The
		// spec is confusing on this point. See RFC 2308.
		return dnsmessage.Name{}, nil, &Error{Err: noSuchHost, Name: name.String(), Server: &server, CacheNegative: false}
	}
	if msg.RCode != dnsmessage.RCodeSuccess {
		// None of the error codes make sense
		// for the query we sent.  If we didn't get
		// a name error and we didn't get success,
		// the server is behaving incorrectly.
		return dnsmessage.Name{}, nil, &Error{Err: "server misbehaving", Name: name.String(), Server: &server}
	}

	// Look for the name.
	// Presotto says it's okay to assume that servers listed in
	// /etc/resolv.conf are recursive resolvers.
	// We asked for recursion, so it should have included
	// all the answers we need in this one packet.
Cname:
	for cnameloop := 0; cnameloop < 10; cnameloop++ {
		addrs = addrs[0:0]
		for _, rr := range msg.Answers {
			h := rr.Header
			if h.Class == dnsmessage.ClassINET && equalASCIILabel(h.Name.String(), name.String()) {
				switch h.Type {
				case qtype:
					addrs = append(addrs, rr)
				case dnsmessage.TypeCNAME:
					// redirect to cname
					name = rr.Body.(*dnsmessage.CNAMEResource).CNAME
					continue Cname
				}
			}
		}
		if len(addrs) == 0 {
			return dnsmessage.Name{}, nil, &Error{Err: noSuchHost, Name: name.String(), Server: &server, CacheNegative: true}
		}
		return name, addrs, nil
	}

	return dnsmessage.Name{}, nil, &Error{Err: "too many redirects", Name: name.String(), Server: &server}
}

func equalASCIILabel(x, y string) bool {
	if len(x) != len(y) {
		return false
	}
	for i := 0; i < len(x); i++ {
		a := x[i]
		b := y[i]
		if 'A' <= a && a <= 'Z' {
			a += 0x20
		}
		if 'A' <= b && b <= 'Z' {
			b += 0x20
		}
		if a != b {
			return false
		}
	}
	return true
}

func isDomainName(s string) bool {
	// See RFC 1035, RFC 3696.
	if len(s) == 0 {
		return false
	}
	if len(s) > 255 {
		return false
	}

	last := byte('.')
	ok := false // Ok once we've seen a letter.
	partlen := 0
	for i := 0; i < len(s); i++ {
		c := s[i]
		switch {
		default:
			return false
		case 'a' <= c && c <= 'z' || 'A' <= c && c <= 'Z' || c == '_':
			ok = true
			partlen++
		case '0' <= c && c <= '9':
			// fine
			partlen++
		case c == '-':
			// Byte before dash cannot be dot.
			if last == '.' {
				return false
			}
			partlen++
		case c == '.':
			// Byte before dot cannot be dot, dash.
			if last == '.' || last == '-' {
				return false
			}
			if partlen > 63 || partlen == 0 {
				return false
			}
			partlen = 0
		}
		last = c
	}
	if last == '-' || partlen > 63 {
		return false
	}

	return ok
}
