// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"bytes"
	"context"
	"fmt"
	"math"
	"time"

	"netstack/util"
	"syslog"

	"github.com/google/netstack/rand"
	"github.com/google/netstack/tcpip"
	tcpipHeader "github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/waiter"
)

const tag = "DHCP"

const defaultLeaseTime = 12 * time.Hour

type AcquiredFunc func(oldAddr, newAddr tcpip.Address, oldSubnet, newSubnet tcpip.Subnet, cfg Config)

// Client is a DHCP client.
type Client struct {
	stack        *stack.Stack
	nicid        tcpip.NICID
	linkAddr     tcpip.LinkAddress
	acquiredFunc AcquiredFunc

	wq waiter.Queue

	addr   tcpip.Address
	server tcpip.Address

	// Used to ensure that only one Run goroutine per interface may be
	// permitted to run at a time. In certain cases, rapidly flapping the
	// DHCP client on and off can cause a second instance of Run to start
	// before the existing one has finished, which can violate invariants.
	// At the time of writing, TestDhcpConfiguration was creating this
	// scenario and causing panics.
	sem chan struct{}
}

type dhcpClientState uint8

const (
	initSelecting dhcpClientState = iota
	renewing
	rebinding
)

// NewClient creates a DHCP client.
//
// acquiredFunc will be called after each DHCP acquisition, and is responsible
// for making necessary modifications to the stack state.
//
// TODO: use (*stack.Stack).NICInfo()[nicid].LinkAddress instead of passing
// linkAddr when broadcasting on multiple interfaces works.
func NewClient(s *stack.Stack, nicid tcpip.NICID, linkAddr tcpip.LinkAddress, acquiredFunc AcquiredFunc) *Client {
	return &Client{
		stack:        s,
		nicid:        nicid,
		linkAddr:     linkAddr,
		acquiredFunc: acquiredFunc,
		sem:          make(chan struct{}, 1),
	}
}

// Run starts the DHCP client.
//
// The function periodically searches for a new IP address.
func (c *Client) Run(ctx context.Context) {
	// For the initial iteration of the acquisition loop, the client should
	// be in the initSelecting state, corresponding to the
	// INIT->SELECTING->REQUESTING->BOUND state transition:
	// https://tools.ietf.org/html/rfc2131#section-4.4
	clientState := initSelecting

	initSelectingTimer := time.NewTimer(math.MaxInt64)
	rebindTimer := time.NewTimer(math.MaxInt64)
	renewTimer := time.NewTimer(math.MaxInt64)

	go func() {
		c.sem <- struct{}{}
		defer func() { <-c.sem }()

		var oldSubnet tcpip.Subnet

		for {
			if err := func() error {
				ctx, cancel := context.WithTimeout(ctx, 3*time.Second)
				defer cancel()

				oldAddr := c.addr

				cfg, err := c.acquire(ctx, clientState)
				if err != nil {
					return err
				}
				subnet, err := tcpip.NewSubnet(util.ApplyMask(c.addr, cfg.SubnetMask), cfg.SubnetMask)
				if err != nil {
					return fmt.Errorf("NewSubnet(%s, %s): %s", c.addr, cfg.SubnetMask, err)
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
					fn(oldAddr, c.addr, oldSubnet, subnet, cfg)
				}
				oldSubnet = subnet

				return nil
			}(); err != nil {
				var timer *time.Timer
				switch clientState {
				case initSelecting:
					timer = initSelectingTimer
				case renewing:
					timer = renewTimer
				case rebinding:
					timer = rebindTimer
				default:
					panic(fmt.Sprintf("unknown client state: clientState=%s", clientState))
				}
				timer.Reset(time.Second)
				syslog.WarnTf(tag, "%s; retrying", err)
			}

			// Attempt complete. Wait for the next event.

			// In the error case, a one second retry timer will have been set. If a state
			// transition timer fires at the same time as the retry timer, then the non-determinism
			// of the selection between the two timers could lead to the client incorrectly bouncing back
			// and forth between two states, e.g. RENEW->REBIND->RENEW. Accordingly, we must check for
			// validity before allowing a state transition to occur.
			var next dhcpClientState
			select {
			case <-ctx.Done():
				// Client was stopped.
				return
			case <-initSelectingTimer.C:
				next = initSelecting
			case <-renewTimer.C:
				next = renewing
			case <-rebindTimer.C:
				next = rebinding
			}
			if clientState <= next || next == initSelecting {
				clientState = next
			}
		}
	}()
}

func (c *Client) acquire(ctx context.Context, clientState dhcpClientState) (Config, error) {
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
		Addr: c.addr,
		Port: ClientPort,
		NIC:  c.nicid,
	}
	writeOpts := tcpip.WriteOptions{
		To: &tcpip.FullAddress{
			Addr: tcpipHeader.IPv4Broadcast,
			Port: ServerPort,
			NIC:  c.nicid,
		},
	}
	switch clientState {
	case initSelecting:
		bindAddress.Addr = tcpipHeader.IPv4Any

		// TODO(NET-2555): remove calls to {Add,Remove}Address when they're
		// no longer required to send and receive broadcast.
		if err := c.stack.AddAddressWithOptions(c.nicid, ipv4.ProtocolNumber, tcpipHeader.IPv4Any, stack.NeverPrimaryEndpoint); err != nil {
			panic(fmt.Sprintf("AddAddressWithOptions(%d, %d, %s): %s", c.nicid, ipv4.ProtocolNumber, tcpipHeader.IPv4Any, err))
		}
		defer func() {
			if err := c.stack.RemoveAddress(c.nicid, tcpipHeader.IPv4Any); err != nil {
				panic(fmt.Sprintf("RemoveAddress(%d, %s): %s", c.nicid, tcpipHeader.IPv4Any, err))
			}
		}()

	case renewing:
		writeOpts.To.Addr = c.server
	case rebinding:
	default:
		panic(fmt.Sprintf("unknown client state: clientState=%s", clientState))
	}
	ep, err := c.stack.NewEndpoint(tcpipHeader.UDPProtocolNumber, tcpipHeader.IPv4ProtocolNumber, &c.wq)
	if err != nil {
		return Config{}, fmt.Errorf("stack.NewEndpoint(): %s", err)
	}
	defer ep.Close()
	// TODO(NET-2441): Use SO_BINDTODEVICE instead of SO_REUSEPORT.
	if err := ep.SetSockOpt(tcpip.ReusePortOption(1)); err != nil {
		return Config{}, fmt.Errorf("SetSockOpt(ReusePortOption): %s", err)
	}
	if writeOpts.To.Addr == tcpipHeader.IPv4Broadcast {
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
	requestedAddr := c.addr
	if clientState == initSelecting {
		discOpts := append(options{
			{optDHCPMsgType, []byte{byte(dhcpDISCOVER)}},
		}, commonOpts...)
		if len(requestedAddr) != 0 {
			discOpts = append(discOpts, option{optReqIPAddr, []byte(requestedAddr)})
		}
		if err := c.send(
			ctx,
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
			return Config{}, fmt.Errorf("%s: %s", dhcpDISCOVER, err)
		}

		// Receive a DHCPOFFER message from a responding DHCP server.
		//
		for {
			srcAddr, addr, opts, typ, err := recv(ctx, ep, ch, xid[:])
			if err != nil {
				return Config{}, fmt.Errorf("recv %s: %s", dhcpOFFER, err)
			}

			if typ != dhcpOFFER {
				syslog.VLogTf(syslog.DebugVerbosity, tag, "got DHCP type = %s, want = %s", typ, dhcpOFFER)
				continue
			}

			var cfg Config
			if err := cfg.decode(opts); err != nil {
				return Config{}, fmt.Errorf("%s decode: %s", typ, err)
			}
			requestedAddr = addr

			// We can overwrite the client's server notion, since there's no
			// atomicity required for correctness.
			//
			// We do not perform sophisticated offer selection and instead merely
			// select the first valid offer we receive.
			c.server = cfg.ServerAddress

			if len(cfg.SubnetMask) == 0 {
				cfg.SubnetMask = util.DefaultMask(c.addr)
			}

			prefixLen := util.PrefixLength(cfg.SubnetMask)

			syslog.VLogTf(syslog.DebugVerbosity, tag, "got %s from %s: Address=%s/%d, server=%s, leaseTime=%s, renewalTime=%s, rebindTime=%s", typ, srcAddr.Addr, requestedAddr, prefixLen, c.server, cfg.LeaseLength, cfg.RenewalTime, cfg.RebindingTime)

			break
		}
	}

	reqOpts := append(options{
		{optDHCPMsgType, []byte{byte(dhcpREQUEST)}},
	}, commonOpts...)
	if clientState == initSelecting {
		reqOpts = append(reqOpts,
			options{
				{optDHCPServer, []byte(c.server)},
				{optReqIPAddr, []byte(requestedAddr)},
			}...)
	}

	if err := c.send(
		ctx,
		ep,
		reqOpts,
		writeOpts,
		xid[:],
		clientState != renewing,      /* broadcast */
		clientState != initSelecting, /* ciaddr */
	); err != nil {
		return Config{}, fmt.Errorf("%s: %s", dhcpREQUEST, err)
	}

	// Receive a DHCPACK/DHCPNAK from the server.
	for {
		fromAddr, addr, opts, typ, err := recv(ctx, ep, ch, xid[:])
		if err != nil {
			return Config{}, fmt.Errorf("recv %s: %s", dhcpACK, err)
		}

		switch typ {
		case dhcpACK:
			var cfg Config
			if err := cfg.decode(opts); err != nil {
				return Config{}, fmt.Errorf("%s decode: %s", typ, err)
			}
			if addr != requestedAddr {
				return Config{}, fmt.Errorf("%s with unexpected address=%s expected=%s", typ, addr, requestedAddr)
			}
			// Now that we've successfully acquired the address, update the client state.
			c.addr = requestedAddr

			syslog.VLogTf(syslog.DebugVerbosity, tag, "got %s from %s", typ, fromAddr.Addr)
			return cfg, nil
		case dhcpNAK:
			if msg := opts.message(); len(msg) != 0 {
				return Config{}, fmt.Errorf("%s: %x", typ, msg)
			}
			return Config{}, fmt.Errorf("empty %s", typ)
		default:
			syslog.VLogTf(syslog.DebugVerbosity, tag, "got DHCP type = %s from %s, want = %s or %s", typ, fromAddr.Addr, dhcpACK, dhcpNAK)
			continue
		}
	}
}

func (c *Client) send(ctx context.Context, ep tcpip.Endpoint, opts options, writeOpts tcpip.WriteOptions, xid []byte, broadcast, ciaddr bool) error {
	h := make(header, headerBaseSize+opts.len()+1)
	h.init()
	h.setOp(opRequest)
	copy(h.xidbytes(), xid)
	if broadcast {
		h.setBroadcast()
	}
	if ciaddr {
		copy(h.ciaddr(), c.addr)
	}

	copy(h.chaddr(), c.linkAddr)
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

func recv(ctx context.Context, ep tcpip.Endpoint, ch <-chan struct{}, xid []byte) (tcpip.FullAddress, tcpip.Address, options, dhcpMsgType, error) {
	for {
		var srcAddr tcpip.FullAddress
		v, _, err := ep.Read(&srcAddr)
		if err == tcpip.ErrWouldBlock {
			select {
			case <-ch:
				continue
			case <-ctx.Done():
				return tcpip.FullAddress{}, "", nil, 0, fmt.Errorf("read: %s", ctx.Err())
			}
		}
		if err != nil {
			return tcpip.FullAddress{}, "", nil, 0, fmt.Errorf("read: %s", err)
		}

		h := header(v)

		if !h.isValid() {
			return tcpip.FullAddress{}, "", nil, 0, fmt.Errorf("invalid header: %x", h)
		}

		if op := h.op(); op != opReply {
			return tcpip.FullAddress{}, "", nil, 0, fmt.Errorf("op-code=%s, want=%s", h, opReply)
		}

		if !bytes.Equal(h.xidbytes(), xid[:]) {
			// This message is for another client, ignore silently.
			continue
		}

		{
			opts, err := h.options()
			if err != nil {
				return tcpip.FullAddress{}, "", nil, 0, fmt.Errorf("invalid options: %s", err)
			}

			typ, err := opts.dhcpMsgType()
			if err != nil {
				return tcpip.FullAddress{}, "", nil, 0, fmt.Errorf("invalid type: %s", err)
			}

			return srcAddr, tcpip.Address(h.yiaddr()), opts, typ, nil
		}
	}
}
