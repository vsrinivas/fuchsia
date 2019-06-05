// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dhcp

import (
	"bytes"
	"context"
	"fmt"
	"time"

	"netstack/util"
	"syslog"

	"github.com/google/netstack/rand"
	"github.com/google/netstack/tcpip"
	tcpipHeader "github.com/google/netstack/tcpip/header"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/stack"
	"github.com/google/netstack/tcpip/transport/udp"
	"github.com/google/netstack/waiter"
)

const tag = "DHCP"

type AcquiredFunc func(oldAddr, newAddr tcpip.Address, oldSubnet, newSubnet tcpip.Subnet, cfg Config)

// Client is a DHCP client.
type Client struct {
	stack        *stack.Stack
	nicid        tcpip.NICID
	linkAddr     tcpip.LinkAddress
	acquiredFunc AcquiredFunc

	addr   tcpip.Address
	subnet tcpip.Subnet
}

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
	}
}

// Run starts the DHCP client.
// It will periodically search for an IP address using the request method.
func (c *Client) Run(ctx context.Context) {
	go func() {
		if err := func() error {
			for {
				oldAddr, oldSubnet := c.addr, c.subnet
				cfg, err := c.runOnce(ctx, oldAddr)
				if err != nil {
					return err
				}
				if fn := c.acquiredFunc; fn != nil {
					fn(oldAddr, c.addr, oldSubnet, c.subnet, cfg)
				}

				select {
				case <-ctx.Done():
					return ctx.Err()
				case <-time.After(cfg.LeaseLength):
					// loop and make a renewal request
				}
			}
		}(); err != nil {
			syslog.VLogTf(syslog.DebugVerbosity, tag, "%s", err)
		}
	}()
}

func (c *Client) runOnce(ctx context.Context, requestedAddr tcpip.Address) (Config, error) {
	// TODO(b/127321246): remove calls to {Add,Remove}Address when they're no
	// longer required to send and receive broadcast.
	if err := c.stack.AddAddressWithOptions(c.nicid, ipv4.ProtocolNumber, tcpipHeader.IPv4Any, stack.NeverPrimaryEndpoint); err != nil && err != tcpip.ErrDuplicateAddress {
		return Config{}, fmt.Errorf("AddAddressWithOptions(): %s", err)
	}
	defer c.stack.RemoveAddress(c.nicid, tcpipHeader.IPv4Any)

	var wq waiter.Queue
	ep, err := c.stack.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, &wq)
	if err != nil {
		return Config{}, fmt.Errorf("NewEndpoint(): %s", err)
	}
	defer ep.Close()

	if err := ep.SetSockOpt(tcpip.BroadcastOption(1)); err != nil {
		return Config{}, fmt.Errorf("SetSockOpt(BroadcastOption): %s", err)
	}
	if err := ep.Bind(tcpip.FullAddress{
		Addr: tcpipHeader.IPv4Any,
		Port: ClientPort,
		NIC:  c.nicid,
	}); err != nil {
		return Config{}, fmt.Errorf("Bind(): %s", err)
	}

	for {
		cfg, err := c.request(ctx, ep, &wq, requestedAddr)
		if err != nil {
			syslog.VLogTf(syslog.DebugVerbosity, tag, "%s; retrying", err)

			select {
			case <-time.After(1 * time.Second):
				continue
			case <-ctx.Done():
				return Config{}, ctx.Err()
			}
		}
		return cfg, nil
	}
}

// request executes a DHCP request session.
//
// On success, it adds a new address to this client's TCPIP stack.
// If the server sets a lease limit a timer is set to automatically
// renew it.
func (c *Client) request(ctx context.Context, ep tcpip.Endpoint, wq *waiter.Queue, requestedAddr tcpip.Address) (Config, error) {
	ctx, cancel := context.WithTimeout(ctx, 3*time.Second)
	defer cancel()

	writeOpts := tcpip.WriteOptions{
		To: &tcpip.FullAddress{
			Addr: tcpipHeader.IPv4Broadcast,
			Port: ServerPort,
			NIC:  c.nicid,
		},
	}

	var xid [4]byte
	if _, err := rand.Read(xid[:]); err != nil {
		return Config{}, fmt.Errorf("rand.Read(): %s", err)
	}

	// DHCPDISCOVER
	{
		discOpts := options{
			{optDHCPMsgType, []byte{byte(dhcpDISCOVER)}},
			{optParamReq, []byte{
				1,  // request subnet mask
				3,  // request router
				15, // domain name
				6,  // domain name server
			}},
		}
		if len(requestedAddr) != 0 {
			discOpts = append(discOpts, option{optReqIPAddr, []byte(requestedAddr)})
		}
		h := make(header, headerBaseSize+discOpts.len()+1)
		h.init()
		h.setOp(opRequest)
		copy(h.xidbytes(), xid[:])
		h.setBroadcast()
		copy(h.chaddr(), c.linkAddr)
		h.setOptions(discOpts)

		if err := write(ctx, ep, tcpip.SlicePayload(h), writeOpts); err != nil {
			return Config{}, fmt.Errorf("discover write: %s", err)
		}
	}

	we, ch := waiter.NewChannelEntry(nil)
	wq.EventRegister(&we, waiter.EventIn)
	defer wq.EventUnregister(&we)

	// DHCPOFFER
	cfg, err := func() (Config, error) {
		for {
			v, _, err := ep.Read(nil)
			if err == tcpip.ErrWouldBlock {
				select {
				case <-ch:
					continue
				case <-ctx.Done():
					return Config{}, fmt.Errorf("reading offer: %s", ctx.Err())
				}
			}
			if err != nil {
				return Config{}, fmt.Errorf("reading offer: %s", err)
			}
			{
				h := header(v)
				if !validateReply(h, xid[:]) {
					syslog.VLogTf(syslog.DebugVerbosity, tag, "invalid header: %x", h)
					continue
				}
				opts, err := h.options()
				if err != nil {
					syslog.VLogTf(syslog.DebugVerbosity, tag, "invalid options: %s", err)
					continue
				}
				typ, err := opts.dhcpMsgType()
				if err != nil {
					syslog.VLogTf(syslog.DebugVerbosity, tag, "invalid type: %s", err)
					continue
				}
				if typ != dhcpOFFER {
					syslog.VLogTf(syslog.DebugVerbosity, tag, "got DHCP type = %d, want = %d", typ, dhcpOFFER)
					continue
				}
				var cfg Config
				if err := cfg.decode(opts); err != nil {
					return Config{}, fmt.Errorf("decoding offer: %s", err)
				}
				c.addr = tcpip.Address(h.yiaddr())
				return cfg, nil
			}
		}
	}()
	if err != nil {
		return Config{}, err
	}

	if len(cfg.SubnetMask) == 0 {
		cfg.SubnetMask = util.DefaultMask(c.addr)
	}

	{
		subnet, err := tcpip.NewSubnet(util.ApplyMask(c.addr, cfg.SubnetMask), cfg.SubnetMask)
		if err != nil {
			return Config{}, fmt.Errorf("NewSubnet(%s, %s): %s", c.addr, cfg.SubnetMask, err)
		}
		c.subnet = subnet
	}

	// DHCPREQUEST
	err = func() error {
		reqOpts := options{
			{optDHCPMsgType, []byte{byte(dhcpREQUEST)}},
			{optReqIPAddr, []byte(c.addr)},
			{optDHCPServer, []byte(cfg.ServerAddress)},
		}
		h := make(header, headerBaseSize+reqOpts.len()+1)
		h.init()
		h.setOp(opRequest)
		copy(h.xidbytes(), xid[:])
		h.setBroadcast()
		copy(h.chaddr(), c.linkAddr)
		h.setOptions(reqOpts)

		if err := write(ctx, ep, tcpip.SlicePayload(h), writeOpts); err != nil {
			return fmt.Errorf("request write: %s", err)
		}

		// DHCPACK
		for {
			v, _, err := ep.Read(nil)
			if err == tcpip.ErrWouldBlock {
				select {
				case <-ch:
					continue
				case <-ctx.Done():
					return fmt.Errorf("reading ack: %s", ctx.Err())
				}
			}
			if err != nil {
				return fmt.Errorf("reading ack: %s", err)
			}
			{
				h := header(v)
				if !validateReply(h, xid[:]) {
					syslog.VLogTf(syslog.DebugVerbosity, tag, "invalid header: %x", h)
					continue
				}
				opts, err := h.options()
				if err != nil {
					syslog.VLogTf(syslog.DebugVerbosity, tag, "invalid options: %s", err)
					continue
				}
				typ, err := opts.dhcpMsgType()
				if err != nil {
					syslog.VLogTf(syslog.DebugVerbosity, tag, "invalid type: %s", err)
					continue
				}

				switch typ {
				case dhcpACK:
					return nil
				case dhcpNAK:
					if msg := opts.message(); len(msg) != 0 {
						return fmt.Errorf("NAK %q", msg)
					}
					return fmt.Errorf("NAK with no message")
				default:
					syslog.VLogTf(syslog.DebugVerbosity, tag, "got DHCP type = %d, want = %d or %d", typ, dhcpACK, dhcpNAK)
					continue
				}
			}
		}
	}()
	if err != nil {
		c.addr = ""
		c.subnet = tcpip.Subnet{}
	}
	return cfg, err
}

func write(ctx context.Context, ep tcpip.Endpoint, payload tcpip.SlicePayload, writeOpts tcpip.WriteOptions) error {
	for {
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

func validateReply(h header, xid []byte) bool {
	if !h.isValid() {
		return false
	}
	if h.op() != opReply {
		return false
	}
	if !bytes.Equal(h.xidbytes(), xid[:]) {
		return false
	}

	return true
}
