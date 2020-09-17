// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"bytes"
	"context"
	"fmt"
	"math/rand"
	"net"
	"sync/atomic"
	"time"

	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/waiter"
)

const (
	tag                        = "DHCP"
	defaultLeaseLength Seconds = 12 * 3600
)

type AcquiredFunc func(oldAddr, newAddr tcpip.AddressWithPrefix, cfg Config)

// Client is a DHCP client.
type Client struct {
	stack *stack.Stack

	// info holds the Client's state as type Info.
	info atomic.Value

	acquiredFunc AcquiredFunc

	wq waiter.Queue

	// Used to ensure that only one Run goroutine per interface may be
	// permitted to run at a time. In certain cases, rapidly flapping the
	// DHCP client on and off can cause a second instance of Run to start
	// before the existing one has finished, which can violate invariants.
	// At the time of writing, TestDhcpConfiguration was creating this
	// scenario and causing panics.
	sem chan struct{}

	stats Stats

	// Stubbable in test.
	rand           *rand.Rand
	retransTimeout func(time.Duration) <-chan time.Time
	acquire        func(ctx context.Context, c *Client, info *Info) (Config, error)
	now            func() time.Time
}

type dhcpClientState uint8

const (
	initSelecting dhcpClientState = iota
	bound
	renewing
	rebinding
)

// Stats collects DHCP statistics per client.
type Stats struct {
	InitAcquire                 tcpip.StatCounter
	RenewAcquire                tcpip.StatCounter
	RebindAcquire               tcpip.StatCounter
	SendDiscovers               tcpip.StatCounter
	RecvOffers                  tcpip.StatCounter
	SendRequests                tcpip.StatCounter
	RecvAcks                    tcpip.StatCounter
	RecvNaks                    tcpip.StatCounter
	SendDiscoverErrors          tcpip.StatCounter
	SendRequestErrors           tcpip.StatCounter
	RecvOfferErrors             tcpip.StatCounter
	RecvOfferUnexpectedType     tcpip.StatCounter
	RecvOfferOptsDecodeErrors   tcpip.StatCounter
	RecvOfferTimeout            tcpip.StatCounter
	RecvOfferAcquisitionTimeout tcpip.StatCounter
	RecvAckErrors               tcpip.StatCounter
	RecvNakErrors               tcpip.StatCounter
	RecvAckOptsDecodeErrors     tcpip.StatCounter
	RecvAckAddrErrors           tcpip.StatCounter
	RecvAckUnexpectedType       tcpip.StatCounter
	RecvAckTimeout              tcpip.StatCounter
	RecvAckAcquisitionTimeout   tcpip.StatCounter
}

type Info struct {
	// NICID is the identifer to the associated NIC.
	NICID tcpip.NICID
	// LinkAddr is the link-address of the associated NIC.
	LinkAddr tcpip.LinkAddress
	// Acquisition is the duration within which a complete DHCP transaction must
	// complete before timing out.
	Acquisition time.Duration
	// Backoff is the duration for which the client must wait before starting a
	// new DHCP transaction after a failed transaction.
	Backoff time.Duration
	// Retransmission is the duration to wait before resending a DISCOVER or
	// REQUEST within an active transaction.
	Retransmission time.Duration
	// Addr is the acquired network address.
	Addr tcpip.AddressWithPrefix
	// Server is the network address of the DHCP server.
	Server tcpip.Address
	// State is the DHCP client state.
	State dhcpClientState
	// OldAddr is the address reported in the last call to acquiredFunc.
	OldAddr tcpip.AddressWithPrefix
}

// NewClient creates a DHCP client.
//
// acquiredFunc will be called after each DHCP acquisition, and is responsible
// for making necessary modifications to the stack state.
//
// TODO: use (*stack.Stack).NICInfo()[nicid].LinkAddress instead of passing
// linkAddr when broadcasting on multiple interfaces works.
func NewClient(s *stack.Stack, nicid tcpip.NICID, linkAddr tcpip.LinkAddress, acquisition, backoff, retransmission time.Duration, acquiredFunc AcquiredFunc) *Client {
	c := &Client{
		stack:          s,
		acquiredFunc:   acquiredFunc,
		sem:            make(chan struct{}, 1),
		rand:           rand.New(rand.NewSource(time.Now().UnixNano())),
		retransTimeout: time.After,
		acquire:        acquire,
		now:            time.Now,
	}
	c.info.Store(Info{
		NICID:          nicid,
		LinkAddr:       linkAddr,
		Acquisition:    acquisition,
		Retransmission: retransmission,
		Backoff:        backoff,
	})
	return c
}

// Info returns a copy of the synchronized state of the Info.
func (c *Client) Info() Info {
	return c.info.Load().(Info)
}

// Stats returns a reference to the Client`s stats.
func (c *Client) Stats() *Stats {
	return &c.stats
}

// Run runs the DHCP client.
//
// The function periodically searches for a new IP address.
func (c *Client) Run(ctx context.Context) {
	info := c.Info()
	// For the initial iteration of the acquisition loop, the client should
	// be in the initSelecting state, corresponding to the
	// INIT->SELECTING->REQUESTING->BOUND state transition:
	// https://tools.ietf.org/html/rfc2131#section-4.4
	info.State = initSelecting

	c.sem <- struct{}{}
	defer func() { <-c.sem }()
	defer func() {
		syslog.WarnTf(tag, "client is stopping, cleaning up")
		c.cleanup(&info)
		// cleanup mutates info.
		c.info.Store(info)
	}()

	var leaseExpirationTime, renewTime, rebindTime time.Time
	var timer *time.Timer

	for {
		if err := func() error {
			acquisitionTimeout := info.Acquisition

			// Adjust the timeout to make sure client is not stuck in retransmission
			// when it should transition to the next state. This can only happen for
			// two time-driven transitions: RENEW->REBIND, REBIND->INIT.
			//
			// Another time-driven transition BOUND->RENEW is not handled here because
			// the client does not have to send out any request during BOUND.
			switch s := info.State; s {
			case initSelecting:
				// Nothing to do. The client is initializing, no leases have been acquired.
				// Thus no times are set for renew, rebind, and lease expiration.
				c.stats.InitAcquire.Increment()
			case renewing:
				c.stats.RenewAcquire.Increment()
				if tilRebind := time.Until(rebindTime); tilRebind < acquisitionTimeout {
					acquisitionTimeout = tilRebind
				}
			case rebinding:
				c.stats.RebindAcquire.Increment()
				if tilLeaseExpire := time.Until(leaseExpirationTime); tilLeaseExpire < acquisitionTimeout {
					acquisitionTimeout = tilLeaseExpire
				}
			default:
				panic(fmt.Sprintf("unexpected state before acquire: %s", s))
			}

			ctx, cancel := context.WithTimeout(ctx, acquisitionTimeout)
			defer cancel()

			cfg, err := c.acquire(ctx, c, &info)
			if err != nil {
				return err
			}

			{
				leaseLength, renewTime, rebindTime := cfg.LeaseLength, cfg.RenewTime, cfg.RebindTime
				if cfg.LeaseLength == 0 {
					syslog.WarnTf(tag, "unspecified lease length, setting default=%s", defaultLeaseLength)
					leaseLength = defaultLeaseLength
				}
				switch {
				case cfg.LeaseLength != 0 && cfg.RenewTime >= cfg.LeaseLength:
					syslog.WarnTf(tag, "invalid renewal time: renewing=%s, lease=%s", cfg.RenewTime, cfg.LeaseLength)
					fallthrough
				case cfg.RenewTime == 0:
					// Based on RFC 2131 Sec. 4.4.5, this defaults to (0.5 * duration_of_lease).
					renewTime = leaseLength / 2
				}
				switch {
				case cfg.RenewTime != 0 && cfg.RebindTime <= cfg.RenewTime:
					syslog.WarnTf(tag, "invalid rebinding time: rebinding=%s, renewing=%s", cfg.RebindTime, cfg.RenewTime)
					fallthrough
				case cfg.RebindTime == 0:
					// Based on RFC 2131 Sec. 4.4.5, this defaults to (0.875 * duration_of_lease).
					rebindTime = leaseLength * 875 / 1000
				}
				cfg.LeaseLength, cfg.RenewTime, cfg.RebindTime = leaseLength, renewTime, rebindTime
			}

			now := c.now()
			leaseExpirationTime = now.Add(cfg.LeaseLength.Duration())
			renewTime = now.Add(cfg.RenewTime.Duration())
			rebindTime = now.Add(cfg.RebindTime.Duration())

			if fn := c.acquiredFunc; fn != nil {
				fn(info.OldAddr, info.Addr, cfg)
			}
			info.OldAddr = info.Addr
			info.State = bound

			return nil
		}(); err != nil {
			if ctx.Err() != nil {
				return
			}
			syslog.VLogTf(syslog.DebugVerbosity, tag, "%s; retrying", err)
		}

		// Synchronize info after attempt to acquire is complete.
		c.info.Store(info)

		// RFC 2131 Section 4.4.5
		// https://tools.ietf.org/html/rfc2131#section-4.4.5
		//
		//   T1 MUST be earlier than T2, which, in turn, MUST be earlier than
		//   the time at which the client's lease will expire.
		var next dhcpClientState
		var waitDuration time.Duration
		switch now := c.now(); {
		case !now.Before(leaseExpirationTime):
			next = initSelecting
		case !now.Before(rebindTime):
			next = rebinding
		case !now.Before(renewTime):
			next = renewing
		default:
			switch s := info.State; s {
			case renewing, rebinding:
				// This means the client is stuck in a bad state, because if
				// the timers are correctly set, previous cases should have matched.
				panic(fmt.Sprintf("invalid client state %s, now=%s, leaseExpirationTime=%s, renewTime=%s, rebindTime=%s", s, now, leaseExpirationTime, renewTime, rebindTime))
			}
			waitDuration = renewTime.Sub(now)
			next = renewing
		}

		// No state transition occurred, the client is retrying.
		if info.State == next {
			waitDuration = info.Backoff
		}

		if timer == nil {
			timer = time.NewTimer(waitDuration)
		} else if waitDuration != 0 {
			timer.Reset(waitDuration)
		}

		if info.State != next && next != renewing {
			// Transition immediately for RENEW->REBIND, REBIND->INIT.
			if ctx.Err() != nil {
				return
			}
		} else {
			select {
			case <-ctx.Done():
				return
			case <-timer.C:
			}
		}

		if info.State != initSelecting && next == initSelecting {
			syslog.WarnTf(tag, "lease time expired, cleaning up")
			c.cleanup(&info)
		}

		info.State = next

		// Synchronize info after any state updates.
		c.info.Store(info)
	}
}

func (c *Client) cleanup(info *Info) {
	if info.OldAddr == (tcpip.AddressWithPrefix{}) {
		return
	}

	// Remove the old address and configuration.
	if fn := c.acquiredFunc; fn != nil {
		fn(info.OldAddr, tcpip.AddressWithPrefix{}, Config{})
	}
	info.OldAddr = tcpip.AddressWithPrefix{}
}

const maxBackoff = 64 * time.Second

// Exponential backoff calculates the backoff delay for this iteration (0-indexed) of retransmission.
//
// RFC 2131 section 4.1
// https://tools.ietf.org/html/rfc2131#section-4.1
//
//   The delay between retransmissions SHOULD be
//   chosen to allow sufficient time for replies from the server to be
//   delivered based on the characteristics of the internetwork between
//   the client and the server.  For example, in a 10Mb/sec Ethernet
//   internetwork, the delay before the first retransmission SHOULD be 4
//   seconds randomized by the value of a uniform random number chosen
//   from the range -1 to +1.  Clients with clocks that provide resolution
//   granularity of less than one second may choose a non-integer
//   randomization value.  The delay before the next retransmission SHOULD
//   be 8 seconds randomized by the value of a uniform number chosen from
//   the range -1 to +1.  The retransmission delay SHOULD be doubled with
//   subsequent retransmissions up to a maximum of 64 seconds.
func (c *Client) exponentialBackoff(iteration uint) time.Duration {
	jitter := time.Duration(c.rand.Int63n(int64(2*time.Second+1))) - time.Second // [-1s, +1s]
	backoff := maxBackoff
	// Guards against overflow.
	if retransmission := c.Info().Retransmission; (maxBackoff/retransmission)>>iteration != 0 {
		backoff = retransmission * (1 << iteration)
	}
	backoff += jitter
	if backoff < 0 {
		return 0
	}
	return backoff
}

func acquire(ctx context.Context, c *Client, info *Info) (Config, error) {
	// https://tools.ietf.org/html/rfc2131#section-4.3.6 Client messages:
	//
	// ---------------------------------------------------------------------
	// |              |INIT-REBOOT  |SELECTING    |RENEWING     |REBINDING |
	// ---------------------------------------------------------------------
	// |broad/unicast |broadcast    |broadcast    |unicast      |broadcast |
	// |server-ip     |MUST NOT     |MUST         |MUST NOT     |MUST NOT  |
	// |requested-ip  |MUST         |MUST         |MUST NOT     |MUST NOT  |
	// |ciaddr        |zero         |zero         |IP address   |IP address|
	// ---------------------------------------------------------------------
	bindAddress := tcpip.FullAddress{
		Addr: info.Addr.Address,
		Port: ClientPort,
		NIC:  info.NICID,
	}
	writeOpts := tcpip.WriteOptions{
		To: &tcpip.FullAddress{
			Addr: header.IPv4Broadcast,
			Port: ServerPort,
			NIC:  info.NICID,
		},
	}

	var sendEP tcpip.Endpoint
	switch info.State {
	case initSelecting:
		bindAddress.Addr = header.IPv4Broadcast

		protocolAddress := tcpip.ProtocolAddress{
			Protocol: ipv4.ProtocolNumber,
			AddressWithPrefix: tcpip.AddressWithPrefix{
				Address:   header.IPv4Any,
				PrefixLen: 0,
			},
		}
		// The IPv4 unspecified/any address should never be used as a primary endpoint.
		if err := c.stack.AddProtocolAddressWithOptions(info.NICID, protocolAddress, stack.NeverPrimaryEndpoint); err != nil {
			panic(fmt.Sprintf("AddProtocolAddressWithOptions(%d, %+v, NeverPrimaryEndpoint): %s", info.NICID, protocolAddress, err))
		}
		defer func() {
			if err := c.stack.RemoveAddress(info.NICID, protocolAddress.AddressWithPrefix.Address); err != nil {
				panic(fmt.Sprintf("RemoveAddress(%d, %s): %s", info.NICID, protocolAddress.AddressWithPrefix.Address, err))
			}
		}()

		// Create a dedicated endpoint for writes with an unspecified source address
		// so it can explicitly bind to the IPv4 unspecified address. We do this so
		// ep (the endpoint we receive on) can bind to the IPv4 broadcast address
		// which is where replies will be sent in response to packets sent from this
		// endpoint. The write endpoint needs to explicitly bind to the unspecified
		// address because it is marked as NeverPrimaryEndpoint and will not be used
		// unless explicitly bound to.
		var err *tcpip.Error
		sendEP, err = c.stack.NewEndpoint(header.UDPProtocolNumber, header.IPv4ProtocolNumber, &waiter.Queue{})
		if err != nil {
			return Config{}, fmt.Errorf("stack.NewEndpoint(%d, %d, _): %s", header.UDPProtocolNumber, header.IPv4ProtocolNumber, err)
		}
		defer sendEP.Close()
		opt := tcpip.BindToDeviceOption(info.NICID)
		if err := sendEP.SetSockOpt(&opt); err != nil {
			return Config{}, fmt.Errorf("send ep SetSockOpt(&%T(%d)): %s", opt, opt, err)
		}
		sendBindAddress := bindAddress
		sendBindAddress.Addr = header.IPv4Any
		if err := sendEP.Bind(sendBindAddress); err != nil {
			return Config{}, fmt.Errorf("send ep Bind(%+v): %s", sendBindAddress, err)
		}

	case renewing:
		writeOpts.To.Addr = info.Server
	case rebinding:
	default:
		panic(fmt.Sprintf("unknown client state: c.State=%s", info.State))
	}
	ep, err := c.stack.NewEndpoint(header.UDPProtocolNumber, header.IPv4ProtocolNumber, &c.wq)
	if err != nil {
		return Config{}, fmt.Errorf("stack.NewEndpoint(): %s", err)
	}
	defer ep.Close()

	// If we don't have a dedicated send endpoint, use ep.
	if sendEP == nil {
		sendEP = ep
	}

	// BindToDevice allows us to have multiple DHCP clients listening to the same
	// IP address and port at the same time so long as the nic is unique.
	opt := tcpip.BindToDeviceOption(info.NICID)
	if err := ep.SetSockOpt(&opt); err != nil {
		return Config{}, fmt.Errorf("send ep SetSockOpt(&%T(%d)): %s", opt, opt, err)
	}
	if writeOpts.To.Addr == header.IPv4Broadcast {
		if err := sendEP.SetSockOptBool(tcpip.BroadcastOption, true); err != nil {
			return Config{}, fmt.Errorf("SetSockOptBool(BroadcastOption, true): %s", err)
		}
	}
	if err := ep.Bind(bindAddress); err != nil {
		return Config{}, fmt.Errorf("Bind(%+v): %s", bindAddress, err)
	}

	we, ch := waiter.NewChannelEntry(nil)
	c.wq.EventRegister(&we, waiter.EventIn)
	defer c.wq.EventUnregister(&we)

	var xid [4]byte
	if _, err := c.rand.Read(xid[:]); err != nil {
		return Config{}, fmt.Errorf("c.rand.Read(): %w", err)
	}

	commonOpts := options{
		{optParamReq, []byte{
			1,  // request subnet mask
			3,  // request router
			15, // domain name
			6,  // domain name server
		}},
	}
	requestedAddr := info.Addr
	if info.State == initSelecting {
		discOpts := append(options{
			{optDHCPMsgType, []byte{byte(dhcpDISCOVER)}},
		}, commonOpts...)
		if len(requestedAddr.Address) != 0 {
			discOpts = append(discOpts, option{optReqIPAddr, []byte(requestedAddr.Address)})
		}
		// TODO(38166): Refactor retransmitDiscover and retransmitRequest

	retransmitDiscover:
		for i := uint(0); ; i++ {
			if err := c.send(
				ctx,
				info,
				sendEP,
				discOpts,
				writeOpts,
				xid[:],
				// DHCPDISCOVER is only performed when the client cannot receive unicast
				// (i.e. it does not have an allocated IP address), so a broadcast reply
				// is always requested, and the client's address is never supplied.
				true,  /* broadcast */
				false, /* ciaddr */
			); err != nil {
				c.stats.SendDiscoverErrors.Increment()
				return Config{}, fmt.Errorf("%s: %w", dhcpDISCOVER, err)
			}
			c.stats.SendDiscovers.Increment()

			// Receive a DHCPOFFER message from a responding DHCP server.
			timeoutCh := c.retransTimeout(c.exponentialBackoff(i))
			for {
				srcAddr, addr, opts, typ, timedOut, err := c.recv(ctx, ep, ch, xid[:], timeoutCh)
				if err != nil {
					if timedOut {
						c.stats.RecvOfferAcquisitionTimeout.Increment()
					} else {
						c.stats.RecvOfferErrors.Increment()
					}
					return Config{}, fmt.Errorf("recv %s: %w", dhcpOFFER, err)
				}
				if timedOut {
					c.stats.RecvOfferTimeout.Increment()
					syslog.VLogTf(syslog.DebugVerbosity, tag, "recv timeout waiting for %s, retransmitting %s", dhcpOFFER, dhcpDISCOVER)
					continue retransmitDiscover
				}

				if typ != dhcpOFFER {
					c.stats.RecvOfferUnexpectedType.Increment()
					syslog.VLogTf(syslog.DebugVerbosity, tag, "got DHCP type = %s, want = %s", typ, dhcpOFFER)
					continue
				}
				c.stats.RecvOffers.Increment()

				var cfg Config
				if err := cfg.decode(opts); err != nil {
					c.stats.RecvOfferOptsDecodeErrors.Increment()
					return Config{}, fmt.Errorf("%s decode: %w", typ, err)
				}

				// We can overwrite the client's server notion, since there's no
				// atomicity required for correctness.
				//
				// We do not perform sophisticated offer selection and instead merely
				// select the first valid offer we receive.
				info.Server = cfg.ServerAddress

				if len(cfg.SubnetMask) == 0 {
					cfg.SubnetMask = tcpip.AddressMask(net.IP(info.Addr.Address).DefaultMask())
				}

				prefixLen, _ := net.IPMask(cfg.SubnetMask).Size()
				requestedAddr = tcpip.AddressWithPrefix{
					Address:   addr,
					PrefixLen: prefixLen,
				}

				syslog.VLogTf(syslog.DebugVerbosity, tag, "got %s from %s: Address=%s, server=%s, leaseLength=%s, renewTime=%s, rebindTime=%s", typ, srcAddr.Addr, requestedAddr, info.Server, cfg.LeaseLength, cfg.RenewTime, cfg.RebindTime)

				break retransmitDiscover
			}
		}
	}

	reqOpts := append(options{
		{optDHCPMsgType, []byte{byte(dhcpREQUEST)}},
	}, commonOpts...)
	if info.State == initSelecting {
		reqOpts = append(reqOpts,
			options{
				{optDHCPServer, []byte(info.Server)},
				{optReqIPAddr, []byte(requestedAddr.Address)},
			}...)
	}

retransmitRequest:
	for i := uint(0); ; i++ {
		if err := c.send(
			ctx,
			info,
			sendEP,
			reqOpts,
			writeOpts,
			xid[:],
			info.State == initSelecting, /* broadcast */
			info.State != initSelecting, /* ciaddr */
		); err != nil {
			c.stats.SendRequestErrors.Increment()
			return Config{}, fmt.Errorf("%s: %w", dhcpREQUEST, err)
		}
		c.stats.SendRequests.Increment()

		// Receive a DHCPACK/DHCPNAK from the server.
		timeoutCh := c.retransTimeout(c.exponentialBackoff(i))
		for {
			fromAddr, addr, opts, typ, timedOut, err := c.recv(ctx, ep, ch, xid[:], timeoutCh)
			if err != nil {
				if timedOut {
					c.stats.RecvAckAcquisitionTimeout.Increment()
				} else {
					c.stats.RecvAckErrors.Increment()
				}
				return Config{}, fmt.Errorf("recv %s: %w", dhcpACK, err)
			}
			if timedOut {
				c.stats.RecvAckTimeout.Increment()
				syslog.VLogTf(syslog.DebugVerbosity, tag, "recv timeout waiting for %s, retransmitting %s", dhcpACK, dhcpREQUEST)
				continue retransmitRequest
			}

			switch typ {
			case dhcpACK:
				var cfg Config
				if err := cfg.decode(opts); err != nil {
					c.stats.RecvAckOptsDecodeErrors.Increment()
					return Config{}, fmt.Errorf("%s decode: %w", typ, err)
				}
				prefixLen, _ := net.IPMask(cfg.SubnetMask).Size()
				addr := tcpip.AddressWithPrefix{
					Address:   addr,
					PrefixLen: prefixLen,
				}
				if addr != requestedAddr {
					c.stats.RecvAckAddrErrors.Increment()
					return Config{}, fmt.Errorf("%s with unexpected address=%s expected=%s", typ, addr, requestedAddr)
				}
				c.stats.RecvAcks.Increment()

				// Now that we've successfully acquired the address, update the client state.
				info.Addr = requestedAddr
				syslog.VLogTf(syslog.DebugVerbosity, tag, "got %s from %s with leaseLength=%s", typ, fromAddr.Addr, cfg.LeaseLength)
				return cfg, nil
			case dhcpNAK:
				if msg := opts.message(); len(msg) != 0 {
					c.stats.RecvNakErrors.Increment()
					return Config{}, fmt.Errorf("%s: %x", typ, msg)
				}
				c.stats.RecvNaks.Increment()
				return Config{}, fmt.Errorf("empty %s", typ)
			default:
				c.stats.RecvAckUnexpectedType.Increment()
				syslog.VLogTf(syslog.DebugVerbosity, tag, "got DHCP type = %s from %s, want = %s or %s", typ, fromAddr.Addr, dhcpACK, dhcpNAK)
				continue
			}
		}
	}
}

func (c *Client) send(ctx context.Context, info *Info, ep tcpip.Endpoint, opts options, writeOpts tcpip.WriteOptions, xid []byte, broadcast, ciaddr bool) error {
	h := make(hdr, headerBaseSize+opts.len()+1)
	h.init()
	h.setOp(opRequest)
	copy(h.xidbytes(), xid)
	if broadcast {
		h.setBroadcast()
	}
	if ciaddr {
		copy(h.ciaddr(), info.Addr.Address)
	}

	copy(h.chaddr(), info.LinkAddr)
	h.setOptions(opts)

	typ, err := opts.dhcpMsgType()
	if err != nil {
		panic(err)
	}

	syslog.VLogTf(syslog.DebugVerbosity, tag, "send %s to %s:%d on NIC:%d (bcast=%t ciaddr=%t)", typ, writeOpts.To.Addr, writeOpts.To.Port, writeOpts.To.NIC, broadcast, ciaddr)

	for {
		payload := tcpip.SlicePayload(h)
		n, resCh, err := ep.Write(payload, writeOpts)
		if resCh != nil {
			if err != tcpip.ErrNoLinkAddress {
				panic(fmt.Sprintf("err=%v inconsistent with presence of resCh", err))
			}
			select {
			case <-resCh:
				continue
			case <-ctx.Done():
				return fmt.Errorf("client address resolution: %w", ctx.Err())
			}
		}
		if err == tcpip.ErrWouldBlock {
			panic(fmt.Sprintf("UDP writes are nonblocking; saw %d/%d", n, len(payload)))
		}
		if err != nil {
			return fmt.Errorf("client write: %s", err)
		}
		return nil
	}
}

func (c *Client) recv(ctx context.Context, ep tcpip.Endpoint, ch <-chan struct{}, xid []byte, timeoutCh <-chan time.Time) (tcpip.FullAddress, tcpip.Address, options, dhcpMsgType, bool, error) {
	for {
		var srcAddr tcpip.FullAddress
		v, _, err := ep.Read(&srcAddr)
		if err == tcpip.ErrWouldBlock {
			select {
			case <-ch:
				continue
			case <-timeoutCh:
				return tcpip.FullAddress{}, "", nil, 0, true, nil
			case <-ctx.Done():
				return tcpip.FullAddress{}, "", nil, 0, true, fmt.Errorf("read: %w", ctx.Err())
			}
		}
		if err != nil {
			return tcpip.FullAddress{}, "", nil, 0, false, fmt.Errorf("read: %s", err)
		}

		h := hdr(v)

		if !h.isValid() {
			return tcpip.FullAddress{}, "", nil, 0, false, fmt.Errorf("invalid hdr: %x", h)
		}

		if op := h.op(); op != opReply {
			return tcpip.FullAddress{}, "", nil, 0, false, fmt.Errorf("op-code=%s, want=%s", h, opReply)
		}

		if !bytes.Equal(h.xidbytes(), xid[:]) {
			// This message is for another client, ignore silently.
			continue
		}

		{
			opts, err := h.options()
			if err != nil {
				return tcpip.FullAddress{}, "", nil, 0, false, fmt.Errorf("invalid options: %w", err)
			}

			typ, err := opts.dhcpMsgType()
			if err != nil {
				return tcpip.FullAddress{}, "", nil, 0, false, fmt.Errorf("invalid type: %w", err)
			}

			return srcAddr, tcpip.Address(h.yiaddr()), opts, typ, false, nil
		}
	}
}
