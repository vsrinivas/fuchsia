// Copyright 2018 The gVisor Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//go:build !build_with_native_toolchain

package dhcp

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"log"
	"runtime"
	stdtime "time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/time"

	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"
	"gvisor.dev/gvisor/pkg/waiter"
)

// Server is a DHCP server.
type Server struct {
	conn      conn
	broadcast tcpip.FullAddress
	addrs     []tcpip.Address // TODO: use a tcpip.AddressMask or range structure
	cfg       Config
	cfgopts   []option // cfg to send to client

	handlers []chan hdr

	mu     sync.Mutex
	leases map[tcpip.LinkAddress]serverLease

	testServerOptions testServerOptions
}

type testServerOptions struct {
	// If set, the server will not set optDHCPServer in messages that do not
	// require it, specifically DHCPACK and DHCPNAK.
	// See: https://www.rfc-editor.org/rfc/rfc2132#section-9.7
	omitServerIdentifierWhenNotRequired bool
	// If set, when responding to DHCPDISCOVER with a DHCPOFFER, the server will
	// send a DHCPOFFER (incorrectly) omitting the Server Identifier before
	// sending a valid DHCPOFFER.
	sendOfferWithoutServerIdentifierFirst bool
	// If set, when responding to DHCPDISCOVER with a DHCPOFFER, the server will
	// send a DHCPOFFER with a body of invalid length before sending a valid
	// DHCPOFFER.
	sendOfferWithInvalidOptionsFirst bool
}

// conn is a blocking read/write network endpoint.
type conn interface {
	Read() ([]byte, tcpip.FullAddress, error)
	Write([]byte, *tcpip.FullAddress) error
}

type epConn struct {
	ctx  context.Context
	wq   *waiter.Queue
	ep   tcpip.Endpoint
	we   waiter.Entry
	inCh chan struct{}
}

func newEPConn(ctx context.Context, wq *waiter.Queue, ep tcpip.Endpoint) *epConn {
	c := &epConn{
		ctx: ctx,
		wq:  wq,
		ep:  ep,
	}
	c.we, c.inCh = waiter.NewChannelEntry(waiter.EventIn)
	wq.EventRegister(&c.we)

	go func() {
		<-ctx.Done()
		wq.EventUnregister(&c.we)
	}()

	return c
}

func (c *epConn) Read() ([]byte, tcpip.FullAddress, error) {
	var b bytes.Buffer
	for {
		res, err := c.ep.Read(&b, tcpip.ReadOptions{
			NeedRemoteAddr: true,
		})
		switch err.(type) {
		case nil:
			return b.Bytes(), res.RemoteAddr, nil
		case *tcpip.ErrWouldBlock:
			select {
			case <-c.inCh:
				continue
			case <-c.ctx.Done():
				return nil, tcpip.FullAddress{}, io.EOF
			}
		default:
			return b.Bytes(), res.RemoteAddr, fmt.Errorf("read: %s", err)
		}
	}
}

func (c *epConn) Write(b []byte, addr *tcpip.FullAddress) error {
	var r bytes.Reader
	r.Reset(b)
	if _, err := c.ep.Write(&r, tcpip.WriteOptions{To: addr}); err != nil {
		return fmt.Errorf("write: %s", err)
	}

	return nil
}

func newEPConnServer(ctx context.Context, stack *stack.Stack, addrs []tcpip.Address, cfg Config, testServerOptions testServerOptions) (*Server, error) {
	wq := new(waiter.Queue)
	ep, err := stack.NewEndpoint(udp.ProtocolNumber, ipv4.ProtocolNumber, wq)
	if err != nil {
		return nil, fmt.Errorf("NewEndpoint: %s", err)
	}
	ep.SocketOptions().SetReusePort(true)
	addr := tcpip.FullAddress{Port: ServerPort}
	if err := ep.Bind(addr); err != nil {
		return nil, fmt.Errorf("Bind(%+v): %s", addr, err)
	}
	ep.SocketOptions().SetBroadcast(true)
	c := newEPConn(ctx, wq, ep)
	return NewServer(ctx, c, addrs, cfg, testServerOptions)
}

// NewServer creates a new DHCP server and begins serving.
// The server continues serving until ctx is done.
func NewServer(ctx context.Context, c conn, addrs []tcpip.Address, cfg Config, testServerOptions testServerOptions) (*Server, error) {
	if cfg.ServerAddress == "" {
		return nil, fmt.Errorf("dhcp: server requires explicit server address")
	}
	s := &Server{
		conn:    c,
		addrs:   addrs,
		cfg:     cfg,
		cfgopts: cfg.encode(),
		broadcast: tcpip.FullAddress{
			Addr: "\xff\xff\xff\xff",
			Port: ClientPort,
		},

		handlers: make([]chan hdr, 8),
		leases:   make(map[tcpip.LinkAddress]serverLease),

		testServerOptions: testServerOptions,
	}

	for i := 0; i < len(s.handlers); i++ {
		ch := make(chan hdr, 8)
		s.handlers[i] = ch
		go s.handler(ctx, ch)
	}

	go s.expirer(ctx)
	go s.reader(ctx)
	return s, nil
}

func (s *Server) expirer(ctx context.Context) {
	t := stdtime.NewTicker(1 * stdtime.Minute)
	defer t.Stop()
	for {
		select {
		// stdtime.Time values produced by stdtime.NewTicker are not comparable with
		// time.Time values so we must ignore the received value from t.C.
		case <-t.C:
			s.mu.Lock()
			for linkAddr, lease := range s.leases {
				if time.Since(lease.start) > s.cfg.LeaseLength.Duration() {
					lease.state = leaseExpired
					s.leases[linkAddr] = lease
				}
			}
			s.mu.Unlock()
		case <-ctx.Done():
			return
		}
	}
}

// reader listens for all incoming DHCP packets and fans them out to
// handling goroutines based on XID as session identifiers.
func (s *Server) reader(ctx context.Context) {
	for {
		v, _, err := s.conn.Read()
		if err != nil {
			return
		}

		h := hdr(v)
		if !h.isValid() || h.op() != opRequest {
			continue
		}
		xid := h.xid()

		// Fan out the packet to a handler goroutine.
		//
		// Use a consistent handler for a given xid, so that
		// packets from a particular client are processed
		// in order.
		ch := s.handlers[int(xid)%len(s.handlers)]
		select {
		case <-ctx.Done():
			return
		case ch <- h:
		default:
			// drop the packet
		}
	}
}

func (s *Server) handler(ctx context.Context, ch chan hdr) {
	for {
		select {
		case h := <-ch:
			if h == nil {
				return
			}
			opts, err := h.options()
			if err != nil {
				continue
			}
			// TODO: Handle DHCPRELEASE and DHCPDECLINE.
			msgType, err := opts.dhcpMsgType()
			if err != nil {
				continue
			}
			switch msgType {
			case dhcpDISCOVER:
				s.handleDiscover(h, opts)
			case dhcpREQUEST:
				s.handleRequest(h, opts)
			}
		case <-ctx.Done():
			return
		}
	}
}

func (s *Server) handleDiscover(hreq hdr, opts options) {
	linkAddr := tcpip.LinkAddress(hreq.chaddr()[:6])
	xid := hreq.xid()

	s.mu.Lock()
	lease := s.leases[linkAddr]
	switch lease.state {
	case leaseNew:
		if len(s.leases) < len(s.addrs) {
			// Find an unused address.
			// TODO: avoid building this state on each request.
			alloced := make(map[tcpip.Address]struct{})
			for _, lease := range s.leases {
				alloced[lease.addr] = struct{}{}
			}
			for _, addr := range s.addrs {
				if _, ok := alloced[addr]; !ok {
					lease = serverLease{
						start: time.Now(),
						addr:  addr,
						xid:   xid,
						state: leaseOffer,
					}
					s.leases[linkAddr] = lease
					break
				}
			}
		} else {
			// No more addresses, take an expired address.
			for k, oldLease := range s.leases {
				if oldLease.state == leaseExpired {
					delete(s.leases, k)
					lease = serverLease{
						start: time.Now(),
						addr:  lease.addr,
						xid:   xid,
						state: leaseOffer,
					}
					s.leases[linkAddr] = lease
					break
				}
			}
			log.Printf("server has no more addresses")
			s.mu.Unlock()
			return
		}
	case leaseOffer, leaseAck, leaseExpired:
		lease = serverLease{
			start: time.Now(),
			addr:  s.leases[linkAddr].addr,
			xid:   xid,
			state: leaseOffer,
		}
		s.leases[linkAddr] = lease
	}
	s.mu.Unlock()

	// DHCPOFFER
	opts = options{
		{optDHCPMsgType, []byte{byte(dhcpOFFER)}},
		{optDHCPServer, []byte(s.cfg.ServerAddress)},
	}
	opts = append(opts, s.cfgopts...)

	sendOffer := func(opts options) {
		h := make(hdr, headerBaseSize+opts.len()+1)
		h.init()
		h.setOp(opReply)
		copy(h.xidbytes(), hreq.xidbytes())
		copy(h.yiaddr(), lease.addr)
		copy(h.chaddr(), hreq.chaddr())
		h.setOptions(opts)
		s.conn.Write(h, &s.broadcast)
	}

	if s.testServerOptions.sendOfferWithoutServerIdentifierFirst {
		sendOffer(filterOptions(func(opt option) bool {
			return opt.code != optDHCPServer
		}, opts))
		runtime.Gosched()
	}

	if s.testServerOptions.sendOfferWithInvalidOptionsFirst {
		opts := append([]option(nil), []option(opts)...)
		opts = append(opts, option{
			optRouter,
			// optRouter is required to have a body with length divisible by 4.
			[]byte{1, 2, 3},
		})
		sendOffer(opts)
		runtime.Gosched()
	}
	sendOffer(opts)
}

func (s *Server) nack(hreq hdr) {
	// DHCPNACK
	opts := options([]option{
		{optDHCPMsgType, []byte{byte(dhcpNAK)}},
		{optDHCPServer, []byte(s.cfg.ServerAddress)},
	})
	if s.testServerOptions.omitServerIdentifierWhenNotRequired {
		// We have to filter all of opts here rather than simply not including
		// optDHCPServer in the literal above because optDHCPServer could also be
		// present in s.cfgopts.
		opts = filterOptions(func(opt option) bool { return opt.code != optDHCPServer }, opts)
	}
	h := make(hdr, headerBaseSize+opts.len()+1)
	h.init()
	h.setOp(opReply)
	copy(h.xidbytes(), hreq.xidbytes())
	copy(h.chaddr(), hreq.chaddr())
	h.setOptions(opts)
	s.conn.Write(h, &s.broadcast)
}

func (s *Server) handleRequest(hreq hdr, opts options) {
	linkAddr := tcpip.LinkAddress(hreq.chaddr()[:6])
	xid := hreq.xid()

	reqopts, err := hreq.options()
	if err != nil {
		s.nack(hreq)
		return
	}
	var reqcfg Config
	if err := reqcfg.decode(reqopts); err != nil {
		s.nack(hreq)
		return
	}
	if reqcfg.ServerAddress != s.cfg.ServerAddress && tcpip.Address(hreq.ciaddr()) == header.IPv4Any {
		// This request is for a different DHCP server. Ignore it.
		return
	}

	s.mu.Lock()
	lease := s.leases[linkAddr]
	switch lease.state {
	case leaseOffer, leaseAck, leaseExpired:
		lease = serverLease{
			start: time.Now(),
			addr:  s.leases[linkAddr].addr,
			xid:   xid,
			state: leaseAck,
		}
		s.leases[linkAddr] = lease
	}
	s.mu.Unlock()

	if lease.state == leaseNew {
		// TODO: NACK or accept request
		return
	}

	// DHCPACK
	opts = []option{
		{optDHCPMsgType, []byte{byte(dhcpACK)}},
		{optDHCPServer, []byte(s.cfg.ServerAddress)},
	}
	opts = append(opts, s.cfgopts...)

	if s.testServerOptions.omitServerIdentifierWhenNotRequired {
		// We have to filter all of opts here rather than simply not including
		// optDHCPServer in the literal above because optDHCPServer could also be
		// present in s.cfgopts.
		opts = filterOptions(func(opt option) bool { return opt.code != optDHCPServer }, opts)
	}

	h := make(hdr, headerBaseSize+opts.len()+1)
	h.init()
	h.setOp(opReply)
	copy(h.xidbytes(), hreq.xidbytes())
	copy(h.yiaddr(), lease.addr)
	copy(h.chaddr(), hreq.chaddr())
	h.setOptions(opts)
	addr := s.broadcast
	if !hreq.broadcast() {
		for _, b := range hreq.ciaddr() {
			if b != 0 {
				addr.Addr = tcpip.Address(hreq.ciaddr())
				break
			}
		}
	}
	s.conn.Write(h, &addr)
}

func filterOptions(pred func(option) bool, opts []option) []option {
	var newOpts []option
	for _, opt := range opts {
		if pred(opt) {
			newOpts = append(newOpts, opt)
		}
	}
	return newOpts
}

type leaseState int

const (
	leaseNew leaseState = iota
	leaseOffer
	leaseAck
	leaseExpired
)

type serverLease struct {
	start time.Time
	addr  tcpip.Address
	xid   uint32
	state leaseState
}
