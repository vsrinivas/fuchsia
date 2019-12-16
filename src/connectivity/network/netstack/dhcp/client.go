// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"bytes"
	"context"
	"fmt"
	"math"
	"net"
	"sync/atomic"
	"time"

	"syslog"

	"github.com/pkg/errors"
	"gvisor.dev/gvisor/pkg/rand"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/waiter"
)

const tag = "DHCP"

const defaultLeaseTime = 12 * time.Hour

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
}

type dhcpClientState uint8

const (
	initSelecting dhcpClientState = iota
	renewing
	rebinding
)

// Stats collects DHCP statistics per client.
type Stats struct {
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
		stack:        s,
		acquiredFunc: acquiredFunc,
		sem:          make(chan struct{}, 1),
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

// Run starts the DHCP client.
//
// The function periodically searches for a new IP address.
func (c *Client) Run(ctx context.Context) {
	info := c.Info()
	// For the initial iteration of the acquisition loop, the client should
	// be in the initSelecting state, corresponding to the
	// INIT->SELECTING->REQUESTING->BOUND state transition:
	// https://tools.ietf.org/html/rfc2131#section-4.4
	info.State = initSelecting

	initSelectingTimer := time.NewTimer(math.MaxInt64)
	rebindTimer := time.NewTimer(math.MaxInt64)
	renewTimer := time.NewTimer(math.MaxInt64)

	go func() {
		c.sem <- struct{}{}
		defer func() { <-c.sem }()
		defer func() {
			syslog.WarnTf(tag, "client is stopping, cleaning up")
			c.cleanup(&info)
			// cleanup mutates info.
			c.info.Store(info)
		}()

		for {
			if err := func() error {
				ctx, cancel := context.WithTimeout(ctx, info.Acquisition)
				defer cancel()

				cfg, err := c.acquire(ctx, &info)
				if err != nil {
					return err
				}
				// Avoid races between lease acquisition and timers firing.
				for _, timer := range []*time.Timer{initSelectingTimer, rebindTimer, renewTimer} {
					if !timer.Stop() {
						// TODO: why does this hang? Cf. https://godoc.org/time#Timer.Stop
						if false {
							<-timer.C
						}
					}
				}

				{
					leaseLength, renewalTime, rebindingTime := cfg.LeaseLength, cfg.RenewalTime, cfg.RebindingTime
					if cfg.LeaseLength == 0 {
						syslog.WarnTf(tag, "unspecified lease length, setting default=%s", defaultLeaseTime)
						leaseLength = defaultLeaseTime
					}
					switch {
					case cfg.LeaseLength != 0 && cfg.RenewalTime >= cfg.LeaseLength:
						syslog.WarnTf(tag, "invalid renewal time: renewing=%s lease=%s", cfg.RenewalTime, cfg.LeaseLength)
						fallthrough
					case cfg.RenewalTime == 0:
						// Based on RFC 2131 Sec. 4.4.5, this defaults to
						// (0.5 * duration_of_lease).
						renewalTime = leaseLength / 2
					}
					switch {
					case cfg.RenewalTime != 0 && cfg.RebindingTime <= cfg.RenewalTime:
						syslog.WarnTf(tag, "invalid rebinding time: rebinding=%s renewing=%s", cfg.RebindingTime, cfg.RenewalTime)
						fallthrough
					case cfg.RebindingTime == 0:
						// Based on RFC 2131 Sec. 4.4.5, this defaults to
						// (0.875 * duration_of_lease).
						rebindingTime = leaseLength * 875 / 1000
					}
					cfg.LeaseLength, cfg.RenewalTime, cfg.RebindingTime = leaseLength, renewalTime, rebindingTime
				}

				initSelectingTimer.Reset(cfg.LeaseLength)
				renewTimer.Reset(cfg.RenewalTime)
				rebindTimer.Reset(cfg.RebindingTime)

				if fn := c.acquiredFunc; fn != nil {
					fn(info.OldAddr, info.Addr, cfg)
				}
				info.OldAddr = info.Addr

				return nil
			}(); err != nil {
				if ctx.Err() != nil {
					return
				}
				var timer *time.Timer
				switch info.State {
				case initSelecting:
					timer = initSelectingTimer
				case renewing:
					timer = renewTimer
				case rebinding:
					timer = rebindTimer
				default:
					panic(fmt.Sprintf("unknown client state: state=%s", info.State))
				}
				timer.Reset(info.Backoff)
				syslog.VLogTf(syslog.DebugVerbosity, tag, "%s; retrying", err)
			}

			// Synchronize info after attempt to acquire is complete.
			c.info.Store(info)

			// Wait for the next event.
			//
			// In the error case, a retry timer will have been set. If a state transition timer fires at the
			// same time as the retry timer, then the non-determinism of the selection between the two timers
			// could lead to the client incorrectly bouncing back and forth between two states, e.g.
			// RENEW->REBIND->RENEW. Accordingly, we must check for validity before allowing a state
			// transition to occur.
			var next dhcpClientState
			select {
			case <-ctx.Done():
				return
			case <-initSelectingTimer.C:
				next = initSelecting
			case <-renewTimer.C:
				next = renewing
			case <-rebindTimer.C:
				next = rebinding
			}

			if info.State != initSelecting && next == initSelecting {
				syslog.WarnTf(tag, "lease time expired, cleaning up")
				c.cleanup(&info)
			}

			if info.State <= next || next == initSelecting {
				info.State = next
			}
			// Synchronize info after any state updates.
			c.info.Store(info)
		}
	}()
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

func (c *Client) acquire(ctx context.Context, info *Info) (Config, error) {
	// https://tools.ietf.org/html/rfc2131#section-4.3.6 Client messages:
	//
	//  ---------------------------------------------------------------------
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
		if err := c.stack.AddProtocolAddress(info.NICID, protocolAddress); err != nil {
			panic(fmt.Sprintf("AddProtocolAddress(%d, %s): %s", info.NICID, protocolAddress.AddressWithPrefix, err))
		}
		defer func() {
			if err := c.stack.RemoveAddress(info.NICID, protocolAddress.AddressWithPrefix.Address); err != nil {
				panic(fmt.Sprintf("RemoveAddress(%d, %s): %s", info.NICID, protocolAddress.AddressWithPrefix.Address, err))
			}
		}()

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
	// TODO(NET-2441): Use SO_BINDTODEVICE instead of SO_REUSEPORT.
	if err := ep.SetSockOpt(tcpip.ReusePortOption(1)); err != nil {
		return Config{}, fmt.Errorf("SetSockOpt(ReusePortOption): %s", err)
	}
	if writeOpts.To.Addr == header.IPv4Broadcast {
		if err := ep.SetSockOpt(tcpip.BroadcastOption(1)); err != nil {
			return Config{}, fmt.Errorf("SetSockOpt(BroadcastOption): %s", err)
		}
	}
	if err := ep.Bind(bindAddress); err != nil {
		return Config{}, fmt.Errorf("Bind(%+v): %s", bindAddress, err)
	}

	we, ch := waiter.NewChannelEntry(nil)
	c.wq.EventRegister(&we, waiter.EventIn)
	defer c.wq.EventUnregister(&we)

	var xid [4]byte
	if _, err := rand.Read(xid[:]); err != nil {
		return Config{}, fmt.Errorf("rand.Read(): %s", err)
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
		for {
			if err := c.send(
				ctx,
				info,
				ep,
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
				return Config{}, fmt.Errorf("%s: %s", dhcpDISCOVER, err)
			}
			c.stats.SendDiscovers.Increment()

			// Receive a DHCPOFFER message from a responding DHCP server.
			for {
				srcAddr, addr, opts, typ, timedOut, err := c.recv(ctx, info, ep, ch, xid[:])
				if err != nil {
					if timedOut {
						c.stats.RecvOfferAcquisitionTimeout.Increment()
					} else {
						c.stats.RecvOfferErrors.Increment()
					}
					return Config{}, errors.Wrapf(err, "recv %s", dhcpOFFER)
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
					return Config{}, fmt.Errorf("%s decode: %s", typ, err)
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

				syslog.VLogTf(syslog.DebugVerbosity, tag, "got %s from %s: Address=%s, server=%s, leaseTime=%s, renewalTime=%s, rebindTime=%s", typ, srcAddr.Addr, requestedAddr, info.Server, cfg.LeaseLength, cfg.RenewalTime, cfg.RebindingTime)

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
	for {
		if err := c.send(
			ctx,
			info,
			ep,
			reqOpts,
			writeOpts,
			xid[:],
			info.State == initSelecting, /* broadcast */
			info.State != initSelecting, /* ciaddr */
		); err != nil {
			c.stats.SendRequestErrors.Increment()
			return Config{}, fmt.Errorf("%s: %s", dhcpREQUEST, err)
		}
		c.stats.SendRequests.Increment()

		// Receive a DHCPACK/DHCPNAK from the server.
		for {
			fromAddr, addr, opts, typ, timedOut, err := c.recv(ctx, info, ep, ch, xid[:])
			if err != nil {
				if timedOut {
					c.stats.RecvAckAcquisitionTimeout.Increment()
				} else {
					c.stats.RecvAckErrors.Increment()
				}
				return Config{}, errors.Wrapf(err, "recv %s", dhcpACK)
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
					return Config{}, fmt.Errorf("%s decode: %s", typ, err)
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
				syslog.VLogTf(syslog.DebugVerbosity, tag, "got %s from %s with lease %s", typ, fromAddr.Addr, cfg.LeaseLength)
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
				return fmt.Errorf("client address resolution: %s", ctx.Err())
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

func (c *Client) recv(ctx context.Context, info *Info, ep tcpip.Endpoint, ch <-chan struct{}, xid []byte) (tcpip.FullAddress, tcpip.Address, options, dhcpMsgType, bool, error) {
	for {
		var srcAddr tcpip.FullAddress
		v, _, err := ep.Read(&srcAddr)
		if err == tcpip.ErrWouldBlock {
			select {
			case <-ch:
				continue
			case <-time.After(info.Retransmission):
				return tcpip.FullAddress{}, "", nil, 0, true, nil
			case <-ctx.Done():
				return tcpip.FullAddress{}, "", nil, 0, true, errors.Wrap(ctx.Err(), "read failed")
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
				return tcpip.FullAddress{}, "", nil, 0, false, fmt.Errorf("invalid options: %s", err)
			}

			typ, err := opts.dhcpMsgType()
			if err != nil {
				return tcpip.FullAddress{}, "", nil, 0, false, fmt.Errorf("invalid type: %s", err)
			}

			return srcAddr, tcpip.Address(h.yiaddr()), opts, typ, false, nil
		}
	}
}
