// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mdns

import (
	"bytes"
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"strings"
	"syscall"
	"unicode/utf8"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	"golang.org/x/net/ipv4"
	"golang.org/x/net/ipv6"
)

// DefaultPort is the mDNS port required of the spec, though this library is port-agnostic.
const DefaultPort int = 5353

type Header struct {
	ID      uint16
	Flags   uint16
	QDCount uint16
	ANCount uint16
	NSCount uint16
	ARCount uint16
}

type Record struct {
	Domain string
	Type   uint16
	Class  uint16
	Flush  bool
	TTL    uint32
	Data   []byte
}

type Question struct {
	Domain  string
	Type    uint16
	Class   uint16
	Unicast bool
}

type Packet struct {
	Header     Header
	Questions  []Question
	Answers    []Record
	Authority  []Record
	Additional []Record
}

// A small struct used to send received UDP packets and
// information about their interface / source address through a channel.
type receivedPacketInfo struct {
	data []byte
	src  net.Addr
	err  error
}

func writeUint16(out io.Writer, val uint16) error {
	buf := make([]byte, 2)
	binary.BigEndian.PutUint16(buf, val)
	_, err := out.Write(buf)
	return err
}

func (h Header) serialize(out io.Writer) error {
	if err := writeUint16(out, h.ID); err != nil {
		return err
	}
	if err := writeUint16(out, h.Flags); err != nil {
		return err
	}
	if err := writeUint16(out, h.QDCount); err != nil {
		return err
	}
	if err := writeUint16(out, h.ANCount); err != nil {
		return err
	}
	if err := writeUint16(out, h.NSCount); err != nil {
		return err
	}
	if err := writeUint16(out, h.ARCount); err != nil {
		return err
	}
	return nil
}

func writeDomain(out io.Writer, domain string) error {
	domain = strings.TrimSuffix(domain, ".")
	parts := strings.Split(domain, ".")
	// TODO(jakehehrlich): Add check that each label is ASCII.
	// TODO(jakehehrlich): Add check that each label is <= 63 in length.
	// TODO(jakehehrlich): Add support for compression.
	for _, dpart := range parts {
		ascii := []byte(dpart)
		if _, err := out.Write([]byte{byte(len(ascii))}); err != nil {
			return err
		}
		if _, err := out.Write(ascii); err != nil {
			return err
		}
	}
	_, err := out.Write([]byte{0})
	return err
}

func (q Question) serialize(out io.Writer) error {
	if err := writeDomain(out, q.Domain); err != nil {
		return err
	}
	if err := writeUint16(out, q.Type); err != nil {
		return err
	}
	var unicast uint16
	if q.Unicast {
		unicast = 1 << 15
	}
	if err := writeUint16(out, unicast|q.Class); err != nil {
		return err
	}
	return nil
}

func writeUint32(out io.Writer, val uint32) error {
	buf := make([]byte, 4)
	binary.BigEndian.PutUint32(buf, val)
	_, err := out.Write(buf)
	return err
}

func (r Record) serialize(out io.Writer) error {
	if err := writeDomain(out, r.Domain); err != nil {
		return err
	}
	if err := writeUint16(out, r.Type); err != nil {
		return err
	}
	var flush uint16
	if r.Flush {
		flush = 1 << 15
	}
	if err := writeUint16(out, flush|r.Class); err != nil {
		return err
	}
	if err := writeUint32(out, r.TTL); err != nil {
		return err
	}
	if err := writeUint16(out, uint16(len(r.Data))); err != nil {
		return err
	}
	if _, err := out.Write(r.Data); err != nil {
		return err
	}
	return nil
}

func (p Packet) serialize(out io.Writer) error {
	if err := p.Header.serialize(out); err != nil {
		return err
	}
	for _, question := range p.Questions {
		if err := question.serialize(out); err != nil {
			return err
		}
	}
	for _, answer := range p.Answers {
		if err := answer.serialize(out); err != nil {
			return err
		}
	}
	for _, authority := range p.Authority {
		if err := authority.serialize(out); err != nil {
			return err
		}
	}
	for _, addon := range p.Additional {
		if err := addon.serialize(out); err != nil {
			return err
		}
	}
	return nil
}

func readUint16(in io.Reader, out *uint16) error {
	buf := make([]byte, 2)
	_, err := in.Read(buf)
	if err != nil {
		return err
	}
	*out = binary.BigEndian.Uint16(buf)
	return nil
}

func (h *Header) deserialize(data []byte, in io.Reader) error {
	if err := readUint16(in, &h.ID); err != nil {
		return err
	}
	if err := readUint16(in, &h.Flags); err != nil {
		return err
	}
	if err := readUint16(in, &h.QDCount); err != nil {
		return err
	}
	if err := readUint16(in, &h.ANCount); err != nil {
		return err
	}
	if err := readUint16(in, &h.NSCount); err != nil {
		return err
	}
	if err := readUint16(in, &h.ARCount); err != nil {
		return err
	}
	return nil
}

func readDomain(data []byte, in io.Reader, domain *string) error {
	// TODO(jakehehrlich): Don't stack overflow when domain contains cycle.

	var d bytes.Buffer
	for {
		sizeBuf := make([]byte, 1)
		if _, err := in.Read(sizeBuf); err != nil {
			return err
		}
		size := sizeBuf[0]
		// A size of zero indicates that we're done.
		if size == 0 {
			break
		}
		// We don't support compressed domains right now.
		if size > 63 {
			if size < 192 {
				return fmt.Errorf("invalid size for label")
			}
			if _, err := in.Read(sizeBuf); err != nil {
				return err
			}
			offset := ((size & 0x3f) << 8) | sizeBuf[0]
			var pDomain string
			readDomain(data, bytes.NewBuffer(data[offset:]), &pDomain)
			if _, err := d.WriteString(pDomain); err != nil {
				return err
			}
			if err := d.WriteByte(byte('.')); err != nil {
				return err
			}
			break
		}
		// Read in the specified bytes (max length 256)
		buf := make([]byte, size)
		if _, err := in.Read(buf); err != nil {
			return err
		}
		// Make sure the string is ASCII
		for _, b := range buf {
			if b >= utf8.RuneSelf {
				return fmt.Errorf("Found non-ASCII byte %v in domain", b)
			}
		}
		// Now add this to a temporary domain
		if _, err := d.Write(buf); err != nil {
			return err
		}
		// Add the trailing "." as seen in the RFC.
		if err := d.WriteByte(byte('.')); err != nil {
			return err
		}
	}
	*domain = string(d.Bytes())
	// Remove the trailing '.' to canonicalize.
	*domain = strings.TrimSuffix(*domain, ".")
	return nil
}

func (q *Question) deserialize(data []byte, in io.Reader) error {
	if err := readDomain(data, in, &q.Domain); err != nil {
		return fmt.Errorf("reading domain: %w", err)
	}
	if err := readUint16(in, &q.Type); err != nil {
		return err
	}
	var tmp uint16
	if err := readUint16(in, &tmp); err != nil {
		return err
	}
	// Extract class and unicast bit.
	q.Unicast = (tmp >> 15) != 0
	q.Class = (tmp << 1) >> 1
	return nil
}

func readUint32(in io.Reader, out *uint32) error {
	buf := make([]byte, 4)
	_, err := in.Read(buf)
	if err != nil {
		return err
	}
	*out = binary.BigEndian.Uint32(buf)
	return nil
}

func (r *Record) deserialize(data []byte, in io.Reader) error {
	if err := readDomain(data, in, &r.Domain); err != nil {
		return err
	}
	if err := readUint16(in, &r.Type); err != nil {
		return err
	}
	var tmp uint16
	if err := readUint16(in, &tmp); err != nil {
		return err
	}
	// Extract class and flush bit.
	r.Flush = (tmp >> 15) != 0
	r.Class = (tmp << 1) >> 1
	if err := readUint32(in, &r.TTL); err != nil {
		return err
	}

	var dataLength uint16
	if err := readUint16(in, &dataLength); err != nil {
		return err
	}
	// Now read the data (max allocation size of 64k)
	r.Data = make([]byte, dataLength)
	if _, err := in.Read(r.Data); err != nil {
		return err
	}
	return nil
}

// TODO(jakehehrlich): Handle truncation.
func (p *Packet) deserialize(data []byte, in io.Reader) error {
	if err := p.Header.deserialize(data, in); err != nil {
		return err
	}
	p.Questions = make([]Question, p.Header.QDCount)
	for i := uint16(0); i < p.Header.QDCount; i++ {
		if err := p.Questions[i].deserialize(data, in); err != nil {
			return err
		}
	}
	p.Answers = make([]Record, p.Header.ANCount)
	for i := uint16(0); i < p.Header.ANCount; i++ {
		if err := p.Answers[i].deserialize(data, in); err != nil {
			return err
		}
	}
	p.Authority = make([]Record, p.Header.NSCount)
	for i := uint16(0); i < p.Header.NSCount; i++ {
		if err := p.Authority[i].deserialize(data, in); err != nil {
			return err
		}
	}
	p.Additional = make([]Record, p.Header.ARCount)
	for i := uint16(0); i < p.Header.ARCount; i++ {
		if err := p.Additional[i].deserialize(data, in); err != nil {
			return err
		}
	}
	return nil
}

// getFlag constructs the flag field of a header for the tiny subset of
// flag options that we need.
// TODO(jakehehrlich): Implement response code error handling.
// TODO(jakehehrlich): Implement truncation.
func getFlag(query bool, authority bool) uint16 {
	var out uint16
	if !query {
		out |= 1
	}
	if authority {
		out |= 1 << 5
	}
	return out
}

const (
	// A is the DNS Type for ipv4
	A = 1
	// AAAA is the DNS Type for ipv6
	AAAA = 28
	// PTR is the DNS Type for domain name pointers
	PTR = 12
	// SRV is the DNS Type for services
	SRV = 33
	// IN is the Internet DNS Class
	IN = 1
)

// IpToDnsRecordType returns either A or AAAA based on the type of ip.
func IpToDnsRecordType(ip net.IP) uint16 {
	if ip4 := ip.To4(); ip4 != nil {
		return A
	} else {
		return AAAA
	}
}

// MDNS is the central interface through which requests are sent and received.
// This implementation is agnostic to use case and asynchronous.
// To handle various responses add Handlers. To send a packet you may use
// either SendTo (generally used for unicast) or Send (generally used for
// multicast).
type MDNS interface {
	// EnableIPv4 enables listening on IPv4 network interfaces.
	EnableIPv4()

	// EnableIPv6 enables listening on IPv6 network interfaces.
	EnableIPv6()

	// SetAddress sets a non-default listen address.
	SetAddress(address string) error

	// SetMCastTTL sets the multicast time to live. If this is set to less
	// than zero it stays at the default. If it is set to zero this will mean
	// no packets can escape the host.
	//
	// Must be no greater than 255.
	SetMCastTTL(ttl int) error

	// AddHandler calls f on every Packet received.
	AddHandler(f func(net.Interface, net.Addr, Packet))

	// AddWarningHandler calls f on every non-fatal error.
	AddWarningHandler(f func(net.Addr, error))

	// AddErrorHandler calls f on every fatal error. After
	// all active handlers are called, m will stop listening and
	// close it's connection so this function will not be called twice.
	AddErrorHandler(f func(error))

	// Start causes m to start listening for mDNS packets on all interfaces on
	// the specified port. Listening will stop if ctx is done.
	Start(ctx context.Context, port int) error

	// Send serializes and sends packet out as a multicast to all interfaces
	// using the port that m is listening on. Note that Start must be
	// called prior to making this call.
	Send(ctx context.Context, packet Packet) error

	// SendTo serializes and sends packet to dst. If dst is a multicast
	// address then packet is multicast to the corresponding group on
	// all interfaces. Note that start must be called prior to making this
	// call.
	SendTo(ctx context.Context, packet Packet, dst *net.UDPAddr) error

	// Close closes all connections.
	Close()
}

type packetConn interface {
	net.PacketConn
	MulticastInterface() (*net.Interface, error)
}

type netConnectionFactory interface {
	MakeUDPSocket(iface *net.Interface, group *net.UDPAddr, ttl int) (packetConn, error)
}

func makeUDPSocket(network string, ifi *net.Interface, group *net.UDPAddr) (*net.UDPConn, error) {
	return net.ListenMulticastUDP(network, ifi, group)
}

type defaultConnectionFactoryV4 struct{}

type ipv4PacketConn struct {
	*ipv4.PacketConn
}

func (c *ipv4PacketConn) ReadFrom(b []byte) (int, net.Addr, error) {
	n, _, addr, err := c.PacketConn.ReadFrom(b)
	return n, addr, err
}

func (c *ipv4PacketConn) WriteTo(b []byte, dst net.Addr) (int, error) {
	return c.PacketConn.WriteTo(b, nil, dst)
}

func (*defaultConnectionFactoryV4) MakeUDPSocket(ifi *net.Interface, group *net.UDPAddr, ttl int) (packetConn, error) {
	conn, err := makeUDPSocket("udp4", ifi, group)
	if err != nil {
		return nil, err
	}
	{
		conn := ipv4.NewPacketConn(conn)
		if ttl >= 0 {
			if err := conn.SetMulticastTTL(ttl); err != nil {
				_ = conn.Close()
				return nil, err
			}
		}
		return &ipv4PacketConn{PacketConn: conn}, nil
	}
}

type defaultConnectionFactoryV6 struct{}

type ipv6PacketConn struct {
	*ipv6.PacketConn
}

func (c *ipv6PacketConn) ReadFrom(b []byte) (int, net.Addr, error) {
	n, _, addr, err := c.PacketConn.ReadFrom(b)
	return n, addr, err
}

func (c *ipv6PacketConn) WriteTo(b []byte, dst net.Addr) (int, error) {
	return c.PacketConn.WriteTo(b, nil, dst)
}

func (*defaultConnectionFactoryV6) MakeUDPSocket(ifi *net.Interface, group *net.UDPAddr, ttl int) (packetConn, error) {
	conn, err := makeUDPSocket("udp6", ifi, group)
	if err != nil {
		return nil, err
	}
	{
		conn := ipv6.NewPacketConn(conn)
		if ttl >= 0 {
			if err := conn.SetMulticastHopLimit(ttl); err != nil {
				_ = conn.Close()
				return nil, err
			}
		}
		return &ipv6PacketConn{PacketConn: conn}, nil
	}
}

type mDNSConnBase struct {
	dst        net.UDPAddr
	ttl        int
	netFactory netConnectionFactory
	conns      []packetConn
}

func (c *mDNSConnBase) SetMCastTTL(ttl int) error {
	if ttl > 255 {
		return fmt.Errorf("TTL outside of valid range: %d", ttl)
	}
	c.ttl = ttl
	return nil
}

func (c *mDNSConnBase) Close() error {
	for _, conn := range c.conns {
		if err := conn.Close(); err != nil {
			return err
		}
	}
	return nil
}

func (c *mDNSConnBase) Listen(ctx context.Context) <-chan receivedPacketInfo {
	ch := make(chan receivedPacketInfo, 1)
	for _, conn := range c.conns {
		ifi, err := conn.MulticastInterface()
		if err != nil {
			ch <- receivedPacketInfo{err: err}
			continue
		}
		go func(conn packetConn) {
			payloadBuf := make([]byte, 1<<16)
			for {
				n, src, err := conn.ReadFrom(payloadBuf)
				if err != nil {
					ch <- receivedPacketInfo{err: err}
					return
				}
				// TODO(https://github.com/golang/go/issues/41854): remove the nil case.
				if ifi == nil {
					logger.Debugf(ctx, "[?]: %s <- %s: %d bytes", conn.LocalAddr(), src, n)
				} else {
					logger.Debugf(ctx, "[%s]: %s <- %s: %d bytes", ifi.Name, conn.LocalAddr(), src, n)
				}
				ch <- receivedPacketInfo{
					data: append([]byte(nil), payloadBuf[:n]...),
					src:  src,
				}
			}
		}(conn)
	}
	return ch
}

func (c *mDNSConnBase) SendTo(ctx context.Context, buf bytes.Buffer, dst *net.UDPAddr) error {
	for _, conn := range c.conns {
		ifi, err := conn.MulticastInterface()
		if err != nil {
			return err
		}
		name := "?"
		// TODO(https://github.com/golang/go/issues/41854): remove the nil check.
		if ifi != nil {
			name = ifi.Name
		}
		n, err := conn.WriteTo(buf.Bytes(), dst)
		if err != nil {
			if err, ok := err.(*net.OpError); ok {
				if err, ok := err.Err.(*os.SyscallError); ok {
					if err, ok := err.Err.(syscall.Errno); ok {
						switch err {
						case syscall.EADDRNOTAVAIL, syscall.ENETUNREACH:
							// These errors observed during local testing:
							//
							// EADDRNOTAVAIL: when an interface has only tentative addresses
							// assigned.
							//
							// ENETUNREACH: when an interface has no address of the requested
							// family.
							//
							// These errors are usually transient (e.g. during QEMU startup).
							logger.Debugf(ctx, "[%s]: %s -> %s: %s; skipping", name, conn.LocalAddr(), dst, err)
							continue
						}
					}
				}
			}
			return err
		}
		logger.Debugf(ctx, "[%s]: %s -> %s: %d bytes", name, conn.LocalAddr(), dst, n)
	}
	return nil
}

func (c *mDNSConnBase) Send(ctx context.Context, buf bytes.Buffer) error {
	return c.SendTo(ctx, buf, &c.dst)
}

func (c *mDNSConnBase) JoinGroup(ifi *net.Interface) error {
	conn, err := c.netFactory.MakeUDPSocket(ifi, &c.dst, c.ttl)
	if err != nil {
		return err
	}
	c.conns = append(c.conns, conn)
	return nil
}

type mDNSConn4 struct {
	mDNSConnBase
}

var defaultMDNSMulticastIPv4 = net.ParseIP("224.0.0.251")

func newMDNSConn4() *mDNSConn4 {
	return &mDNSConn4{
		mDNSConnBase: mDNSConnBase{
			dst: net.UDPAddr{
				IP: defaultMDNSMulticastIPv4,
			},
			netFactory: &defaultConnectionFactoryV4{},
			ttl:        -1,
		}}
}

type mDNSConn6 struct {
	mDNSConnBase
}

var defaultMDNSMulticastIPv6 = net.ParseIP("ff02::fb")

func newMDNSConn6() *mDNSConn6 {
	return &mDNSConn6{
		mDNSConnBase: mDNSConnBase{
			dst: net.UDPAddr{
				IP: defaultMDNSMulticastIPv6,
			},
			netFactory: &defaultConnectionFactoryV6{},
			ttl:        -1,
		}}
}

type mDNS struct {
	conn4     *mDNSConn4
	conn6     *mDNSConn6
	port      int
	pHandlers []func(net.Addr, Packet)
	wHandlers []func(net.Addr, error)
	eHandlers []func(error)
}

// NewMDNS creates a new object implementing the MDNS interface. Do not forget
// to call EnableIPv4() or EnableIPv6() to enable listening on interfaces of
// the corresponding type, or nothing will work.
func NewMDNS() *mDNS {
	m := mDNS{}
	m.conn4 = nil
	m.conn6 = nil
	return &m
}

func (m *mDNS) EnableIPv4() {
	if m.conn4 == nil {
		m.conn4 = newMDNSConn4()
	}
}

func (m *mDNS) EnableIPv6() {
	if m.conn6 == nil {
		m.conn6 = newMDNSConn6()
	}
}

func (m *mDNS) Close() {
	if m.conn4 != nil {
		m.conn4.Close()
		m.conn4 = nil
	}
	if m.conn6 != nil {
		m.conn6.Close()
		m.conn6 = nil
	}
}

func (m *mDNS) SetAddress(address string) error {
	ip := net.ParseIP(address)
	if ip4 := ip.To4(); ip4 != nil {
		if m.conn4 == nil {
			return fmt.Errorf("mDNS IPv4 support is disabled")
		}
		m.conn4.dst.IP = ip4
	} else if ip16 := ip.To16(); ip16 != nil {
		if m.conn6 == nil {
			return fmt.Errorf("mDNS IPv6 support is disabled")
		}
		m.conn6.dst.IP = ip16
	} else {
		return fmt.Errorf("not a valid IP address: %s", address)
	}
	return nil
}

func (m *mDNS) SetMCastTTL(ttl int) error {
	if m.conn4 != nil {
		if err := m.conn4.SetMCastTTL(ttl); err != nil {
			return err
		}
	}
	if m.conn6 != nil {
		if err := m.conn6.SetMCastTTL(ttl); err != nil {
			return err
		}
	}
	return nil
}

// AddHandler calls f on every Packet received.
func (m *mDNS) AddHandler(f func(net.Addr, Packet)) {
	m.pHandlers = append(m.pHandlers, f)
}

// AddWarningHandler calls f on every non-fatal error.
func (m *mDNS) AddWarningHandler(f func(net.Addr, error)) {
	m.wHandlers = append(m.wHandlers, f)
}

// AddErrorHandler calls f on every fatal error. After
// all active handlers are called, m will stop listening and
// close it's connection so this function will not be called twice.
func (m *mDNS) AddErrorHandler(f func(error)) {
	m.eHandlers = append(m.eHandlers, f)
}

// SendTo serializes and sends packet to dst. If dst is a multicast
// address then packet is multicast to the corresponding group on
// all interfaces. Note that start must be called prior to making this
// call.
func (m *mDNS) SendTo(ctx context.Context, packet Packet, dst *net.UDPAddr) error {
	var buf bytes.Buffer
	// TODO(jakehehrlich): Add checking that the packet is well formed.
	if err := packet.serialize(&buf); err != nil {
		return err
	}
	if dst.IP.To4() != nil {
		if m.conn4 != nil {
			return m.conn4.SendTo(ctx, buf, dst)
		} else {
			return fmt.Errorf("IPv4 was not enabled!")
		}
	} else {
		if m.conn6 != nil {
			return m.conn6.SendTo(ctx, buf, dst)
		} else {
			return fmt.Errorf("IPv6 was not enabled!")
		}
	}
}

// Send serializes and sends packet out as a multicast to all interfaces
// using the port that m is listening on. Note that Start must be
// called prior to making this call.
func (m *mDNS) Send(ctx context.Context, packet Packet) error {
	var buf bytes.Buffer
	// TODO(jakehehrlich): Add checking that the packet is well formed.
	if err := packet.serialize(&buf); err != nil {
		return err
	}
	var err4 error
	if m.conn4 != nil {
		err4 = m.conn4.Send(ctx, buf)
	}
	var err6 error
	if m.conn6 != nil {
		err6 = m.conn6.Send(ctx, buf)
	}
	if err4 != nil {
		return err4
	}
	return err6
}

func (m *mDNS) initMDNSConns(ctx context.Context, port int) error {
	if m.conn4 == nil && m.conn6 == nil {
		return fmt.Errorf("no connections active")
	}
	if c := m.conn4; c != nil {
		c.dst.Port = port
	}
	if c := m.conn6; c != nil {
		c.dst.Port = port
	}
	ifaces, err := net.Interfaces()
	if err != nil {
		return fmt.Errorf("listing interfaces: %w", err)
	}
	connected := false
	for i, iface := range ifaces {
		if iface.Flags&net.FlagMulticast == 0 {
			continue
		}
		if c := m.conn4; c != nil {
			if err := c.JoinGroup(&ifaces[i]); err != nil {
				return fmt.Errorf("failed to join %s: %w", iface.Name, err)
			}
		}
		if c := m.conn6; c != nil {
			if err := c.JoinGroup(&ifaces[i]); err != nil {
				return fmt.Errorf("failed to join %s: %w", iface.Name, err)
			}
		}
		connected = true
	}
	if !connected {
		return fmt.Errorf("no multicast-capable interfaces are up")
	}

	return nil
}

// Start causes m to start listening for MDNS packets on all interfaces on
// the specified port. Listening will stop if ctx is done.
func (m *mDNS) Start(ctx context.Context, port int) error {
	if err := m.initMDNSConns(ctx, port); err != nil {
		m.Close()
		return err
	}
	go func() {
		// NOTE: This defer statement will close connections, which will force
		// the goroutines started by Listen() to exit.
		defer m.Close()

		var chan4 <-chan receivedPacketInfo
		var chan6 <-chan receivedPacketInfo

		if m.conn4 != nil {
			chan4 = m.conn4.Listen(ctx)
		}
		if m.conn6 != nil {
			chan6 = m.conn6.Listen(ctx)
		}
		for {
			var received receivedPacketInfo

			select {
			case <-ctx.Done():
				return
			case received = <-chan4:
				break
			case received = <-chan6:
				break
			}

			if received.err != nil {
				for _, e := range m.eHandlers {
					go e(received.err)
				}
				return
			}

			var packet Packet
			if err := packet.deserialize(received.data, bytes.NewBuffer(received.data)); err != nil {
				for _, w := range m.wHandlers {
					go w(received.src, err)
				}
				continue
			}

			for _, p := range m.pHandlers {
				go p(received.src, packet)
			}
		}
	}()
	return nil
}

// QuestionPacket constructs and returns a packet that
// requests the ip address associated with domain.
func QuestionPacket(domain string) Packet {
	return Packet{
		Header: Header{QDCount: 2},
		Questions: []Question{
			{
				Domain:  domain,
				Type:    AAAA,
				Class:   IN,
				Unicast: false,
			},
			{
				Domain:  domain,
				Type:    A,
				Class:   IN,
				Unicast: false,
			},
		},
	}
}

// AnswerPacket constructs and returns a packet that
// gives a response to the
func AnswerPacket(domain string, ip net.IP) Packet {
	return Packet{
		Header: Header{ANCount: 1},
		Answers: []Record{
			{
				Domain: domain,
				Type:   IpToDnsRecordType(ip),
				Class:  IN,
				Flush:  false,
				Data:   []byte(ip),
			},
		},
	}
}
