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
	"log"
	"net"
	"strings"
	"syscall"
	"unicode/utf8"

	"golang.org/x/net/ipv4"
	"golang.org/x/net/ipv6"
	"golang.org/x/sys/unix"
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
	data  []byte
	iface *net.Interface
	src   net.Addr
	err   error
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

	// Sets whether to accept unicast responses. These can be received when
	// the receiving device is in a different subnet, or if there is port
	// forwarding occurring between the host and the device.
	SetAcceptUnicastResponses(accept bool)

	// ipToSend returns the IP corresponding to the current address.
	ipToSend() net.IP

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
	Send(packet Packet) error

	// SendTo serializes and sends packet to dst. If dst is a multicast
	// address then packet is multicast to the corresponding group on
	// all interfaces. Note that start must be called prior to making this
	// call.
	SendTo(packet Packet, dst *net.UDPAddr) error

	// Close closes all connections.
	Close()
}

type netConnectionFactory interface {
	MakeUDPSocket(port int, ttl int, ip net.IP, iface *net.Interface) (net.PacketConn, error)
	MakePacketReceiver(conn net.PacketConn, v6 bool) packetReceiver
}

// Default connection factory that implementes |netConnectionFactory| interface.
type defaultConnectionFactory struct{}

func (r *defaultConnectionFactory) MakeUDPSocket(port, ttl int, ip net.IP, iface *net.Interface) (net.PacketConn, error) {
	is_ipv6 := ip.To4() == nil
	network := "udp4"
	var zone string
	if is_ipv6 {
		network = "udp6"
		zone = iface.Name
	}
	address := (&net.UDPAddr{IP: ip, Port: port, Zone: zone}).String()

	// A net.ListenConfig control function to set both SO_REUSEADDR and SO_REUSEPORT
	// on a given socket fd. Only works on Unix-based systems, not Windows. For portability
	// an alternative might be to use something like the go-reuseport library.
	control := func(network, address string, c syscall.RawConn) error {
		var err error
		c.Control(func(fd uintptr) {
			err = unix.SetsockoptInt(int(fd), unix.SOL_SOCKET, unix.SO_REUSEADDR, 1)
			if err != nil {
				return
			}

			err = unix.SetsockoptInt(int(fd), unix.SOL_SOCKET, unix.SO_REUSEPORT, 1)
			if err != nil {
				return
			}
			if ttl >= 0 {
				if is_ipv6 {
					err = unix.SetsockoptInt(int(fd), unix.IPPROTO_IPV6, unix.IPV6_MULTICAST_HOPS, ttl)
					if err != nil {
						return
					}
				} else {
					err = unix.SetsockoptInt(int(fd), unix.IPPROTO_IP, unix.IP_MULTICAST_TTL, ttl)
					if err != nil {
						return
					}
				}
			}
		})
		return err
	}

	listenConfig := net.ListenConfig{Control: control}
	return listenConfig.ListenPacket(context.Background(), network, address)
}

func (r *defaultConnectionFactory) MakePacketReceiver(conn net.PacketConn, v6 bool) packetReceiver {
	if v6 {
		conn6 := ipv6.NewPacketConn(conn)
		conn6.SetControlMessage(ipv6.FlagDst|ipv6.FlagInterface, true)
		return &packetReceiver6{conn6}
	}
	conn4 := ipv4.NewPacketConn(conn)
	conn4.SetControlMessage(ipv4.FlagDst|ipv4.FlagInterface, true)
	return &packetReceiver4{conn4}
}

// Interface used to read packets into a caller-owned buffer.
type packetReceiver interface {
	// Reads a packet from the connection into |buf|.  On success, returns
	// the packet size in bytes, the interface |iface| on which it was
	// received, and the source address |src|.
	//
	// On error, returns a non-nil |err|.
	ReadPacket(buf []byte) (size int, iface *net.Interface, src net.Addr, err error)
	// Implements ipv4/ipv6 JoinGroup functionality, joining a group address
	// on the interface |iface|.
	JoinGroup(iface *net.Interface, group net.Addr) error
	Close() error
}

type packetReceiver4 struct {
	conn *ipv4.PacketConn
}

func (p *packetReceiver4) ReadPacket(buf []byte) (size int, iface *net.Interface, src net.Addr, err error) {
	var cm *ipv4.ControlMessage
	size, cm, src, err = p.conn.ReadFrom(buf)
	if err != nil {
		return
	}
	iface, err = net.InterfaceByIndex(cm.IfIndex)
	return
}

func (p *packetReceiver4) JoinGroup(iface *net.Interface, group net.Addr) error {
	return p.conn.JoinGroup(iface, group)
}

func (p *packetReceiver4) Close() error {
	return p.conn.Close()
}

type packetReceiver6 struct {
	conn *ipv6.PacketConn
}

func (p *packetReceiver6) ReadPacket(buf []byte) (size int, iface *net.Interface, src net.Addr, err error) {
	var cm *ipv6.ControlMessage
	size, cm, src, err = p.conn.ReadFrom(buf)
	if err != nil {
		return
	}
	iface, err = net.InterfaceByIndex(cm.IfIndex)
	return
}

func (p *packetReceiver6) JoinGroup(iface *net.Interface, group net.Addr) error {
	return p.conn.JoinGroup(iface, group)
}

func (p *packetReceiver6) Close() error {
	return p.conn.Close()
}

type mDNSConn interface {
	Close() error
	SetIp(ip net.IP) error
	getIp() net.IP
	SetAcceptUnicastResponses(accept bool)
	SendTo(buf bytes.Buffer, dst *net.UDPAddr) error
	Send(buf bytes.Buffer) error
	InitReceiver(port int) error
	// Starts a goroutine to listen for packets incoming on the multicast
	// connection.
	//
	// If |SetAcceptUnicastResponses| has been set to true, starts a
	// separate goroutine to listen to incoming unicast packets for each
	// of the machine's interfaces.
	//
	// Returns a read-only channel on which received packets are written.
	Listen() <-chan receivedPacketInfo
	JoinGroup(iface net.Interface) error
	// Connects to the specific port and interface.
	//
	// This is primarily for sending packets, but if
	// |SetAcceptUnicastResponses(true)| has been called, then this
	// connection becomes bidirectional, and is also used for reading
	// packets when |Listen()| is later invoked.
	//
	// If |SetAcceptUnicastResponses| is called between multiple invocations
	// of this function, then not all connections will be read from when
	// |Listen| is called.
	ConnectTo(port int, ip net.IP, iface *net.Interface) error
	// SetMCastTTL sets the multicast time to live. If this is set to less
	// than zero it stays at the default. If it is set to zero this will mean
	// no packets can escape the host.
	//
	// Must be no greater than 255.
	SetMCastTTL(ttl int) error
	ReadFrom(buf []byte) (size int, iface *net.Interface, src net.Addr, err error)
}

type mDNSConnBase struct {
	dst            net.UDPAddr
	acceptUnicast  bool
	ttl            int
	receiver       packetReceiver
	netFactory     netConnectionFactory
	senders        []net.PacketConn
	ucastReceivers []packetReceiver
}

func (c *mDNSConnBase) SetMCastTTL(ttl int) error {
	if ttl > 255 {
		return fmt.Errorf("TTL outside of valid range: %d", ttl)
	}
	c.ttl = ttl
	return nil
}

func (c *mDNSConnBase) Close() error {
	if c.receiver != nil {
		if err := c.receiver.Close(); err != nil {
			return err
		}
	}
	for _, receiver := range c.ucastReceivers {
		if err := receiver.Close(); err != nil {
			return err
		}
	}
	return nil
}

func (c *mDNSConnBase) Listen() <-chan receivedPacketInfo {
	ch := make(chan receivedPacketInfo, 1)
	startListenLoop(c.receiver, ch)
	for _, receiver := range c.ucastReceivers {
		startListenLoop(receiver, ch)
	}
	return ch
}

func (c *mDNSConnBase) getIp() net.IP {
	return c.dst.IP
}

func (c *mDNSConnBase) SendTo(buf bytes.Buffer, dst *net.UDPAddr) error {
	for _, sender := range c.senders {
		if _, err := sender.WriteTo(buf.Bytes(), dst); err != nil {
			return err
		}
	}
	return nil
}

func (c *mDNSConnBase) SetAcceptUnicastResponses(accept bool) {
	c.acceptUnicast = accept
}

func (c *mDNSConnBase) Send(buf bytes.Buffer) error {
	return c.SendTo(buf, &c.dst)
}

type mDNSConn4 struct {
	mDNSConnBase
}

var defaultMDNSMulticastIPv4 = net.ParseIP("224.0.0.251")

// Helper function for the |Listen| method in the |mDNSConn| interface.
//
// Accepts write-only channel of receivePacketInfo items.
//
// Starts a goroutine which will stop when it cannot read anymore from
// |receiver|, which will happen in the case of an error (like the connection
// being closed).  In this case it will send a final receivedPacketInfo instance
// with only an error code before exiting.
func startListenLoop(receiver packetReceiver, ch chan<- receivedPacketInfo) {
	go func() {
		payloadBuf := make([]byte, 1<<16)
		for {

			size, iface, src, err := receiver.ReadPacket(payloadBuf)
			if err != nil {
				ch <- receivedPacketInfo{err: err}
				return
			}
			data := make([]byte, size)
			copy(data, payloadBuf[:size])
			ch <- receivedPacketInfo{
				data:  data,
				iface: iface,
				src:   src,
				err:   nil}
		}
	}()
}

func newMDNSConn4() mDNSConn {
	c := mDNSConn4{mDNSConnBase{
		netFactory: &defaultConnectionFactory{},
		ttl:        -1,
	}}
	c.SetIp(defaultMDNSMulticastIPv4)
	return &c
}

func (c *mDNSConn4) SetIp(ip net.IP) error {
	if ip4 := ip.To4(); ip4 == nil {
		panic(fmt.Errorf("Not an IPv-4 address: %v", ip))
	}
	c.dst.IP = ip
	return nil
}

func (c *mDNSConn4) InitReceiver(port int) error {
	c.dst.Port = port
	conn, err := net.ListenUDP("udp4", &c.dst)
	if err != nil {
		return err
	}
	c.receiver = c.netFactory.MakePacketReceiver(conn, false)
	return nil
}

// This allows us to listen on this specific interface.
func (c *mDNSConn4) JoinGroup(iface net.Interface) error {
	if err := c.receiver.JoinGroup(&iface, &c.dst); err != nil {
		return fmt.Errorf("joining addr %q on iface %q: %w", &c.dst, iface.Name, err)
	}
	return nil
}

func (c *mDNSConn4) ConnectTo(port int, ip net.IP, iface *net.Interface) error {
	ip4 := ip.To4()
	if ip4 == nil {
		return fmt.Errorf("not a valid IPv4 address: %v", ip)
	}
	conn, err := c.netFactory.MakeUDPSocket(port, c.ttl, ip4, iface)
	if err != nil {
		return err
	}
	c.senders = append(c.senders, conn)
	if c.acceptUnicast {
		newReceiver := c.netFactory.MakePacketReceiver(conn, false)
		c.ucastReceivers = append(c.ucastReceivers, newReceiver)
	}
	return nil
}

func (c *mDNSConn4) ReadFrom(buf []byte) (int, *net.Interface, net.Addr, error) {
	return c.receiver.ReadPacket(buf)
}

type mDNSConn6 struct {
	mDNSConnBase
}

var defaultMDNSMulticastIPv6 = net.ParseIP("ff02::fb")

func newMDNSConn6() mDNSConn {
	c := mDNSConn6{mDNSConnBase{
		netFactory: &defaultConnectionFactory{},
		ttl:        -1,
	}}
	c.SetIp(defaultMDNSMulticastIPv6)
	return &c
}

func (c *mDNSConn6) SetIp(ip net.IP) error {
	if ip6 := ip.To16(); ip6 == nil {
		panic(fmt.Errorf("Not an IPv6 address: %v", ip))
	}
	c.dst.IP = ip
	return nil
}

func (c *mDNSConn6) InitReceiver(port int) error {
	c.dst.Port = port
	conn, err := net.ListenUDP("udp6", &c.dst)
	if err != nil {
		return err
	}
	c.receiver = c.netFactory.MakePacketReceiver(conn, true)
	return nil
}

// This allows us to listen on this specific interface.
func (c *mDNSConn6) JoinGroup(iface net.Interface) error {
	if err := c.receiver.JoinGroup(&iface, &c.dst); err != nil {
		return fmt.Errorf("joining %v%%%v: %w", iface, c.dst, err)
	}
	return nil
}

func (c *mDNSConn6) ConnectTo(port int, ip net.IP, iface *net.Interface) error {
	ip6 := ip.To16()
	if ip6 == nil {
		return fmt.Errorf("not a valid IPv6 address: %v", ip)
	}
	conn, err := c.netFactory.MakeUDPSocket(port, c.ttl, ip6, iface)
	if err != nil {
		return err
	}
	c.senders = append(c.senders, conn)
	if c.acceptUnicast {
		newReceiver := c.netFactory.MakePacketReceiver(conn, true)
		c.ucastReceivers = append(c.ucastReceivers, newReceiver)
	}
	return nil
}

func (c *mDNSConn6) ReadFrom(buf []byte) (int, *net.Interface, net.Addr, error) {
	return c.receiver.ReadPacket(buf)
}

type mDNS struct {
	conn4     mDNSConn
	conn6     mDNSConn
	port      int
	pHandlers []func(net.Interface, net.Addr, Packet)
	wHandlers []func(net.Addr, error)
	eHandlers []func(error)
}

// NewMDNS creates a new object implementing the MDNS interface. Do not forget
// to call EnableIPv4() or EnableIPv6() to enable listening on interfaces of
// the corresponding type, or nothing will work.
func NewMDNS() MDNS {
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

func (m *mDNS) SetAcceptUnicastResponses(accept bool) {
	if m.conn4 != nil {
		m.conn4.SetAcceptUnicastResponses(accept)
	}
	if m.conn6 != nil {
		m.conn6.SetAcceptUnicastResponses(accept)
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
	if ip == nil {
		return fmt.Errorf("Not a valid IP address: %v", address)
	}
	if ip4 := ip.To4(); ip4 != nil {
		if m.conn4 == nil {
			return fmt.Errorf("mDNS IPv4 support is disabled")
		}
		return m.conn4.SetIp(ip4)
	} else {
		if m.conn6 == nil {
			return fmt.Errorf("mDNS IPv6 support is disabled")
		}
		return m.conn6.SetIp(ip.To16())
	}
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

func (m *mDNS) ipToSend() net.IP {
	if m.conn4 != nil {
		return m.conn4.getIp()
	}
	if m.conn6 != nil {
		return m.conn6.getIp()
	}
	return nil
}

// AddHandler calls f on every Packet received.
func (m *mDNS) AddHandler(f func(net.Interface, net.Addr, Packet)) {
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
func (m *mDNS) SendTo(packet Packet, dst *net.UDPAddr) error {
	var buf bytes.Buffer
	// TODO(jakehehrlich): Add checking that the packet is well formed.
	if err := packet.serialize(&buf); err != nil {
		return err
	}
	if dst.IP.To4() != nil {
		if m.conn4 != nil {
			return m.conn4.SendTo(buf, dst)
		} else {
			return fmt.Errorf("IPv4 was not enabled!")
		}
	} else {
		if m.conn6 != nil {
			return m.conn6.SendTo(buf, dst)
		} else {
			return fmt.Errorf("IPv6 was not enabled!")
		}
	}
}

// Send serializes and sends packet out as a multicast to all interfaces
// using the port that m is listening on. Note that Start must be
// called prior to making this call.
func (m *mDNS) Send(packet Packet) error {
	var buf bytes.Buffer
	// TODO(jakehehrlich): Add checking that the packet is well formed.
	if err := packet.serialize(&buf); err != nil {
		return err
	}
	var err4 error
	if m.conn4 != nil {
		err4 = m.conn4.Send(buf)
	}
	var err6 error
	if m.conn6 != nil {
		err6 = m.conn6.Send(buf)
	}
	if err4 != nil {
		return err4
	}
	return err6
}

// connectToAnyAddr takes an mDNSConn and attempts to connect to the first
// available addr on the interface.
func connectToAnyAddr(c mDNSConn, iface *net.Interface, addrs []net.Addr, port int, ipv6 bool) (bool, error) {
	if c == nil {
		return false, nil
	}

	var lastConnectErr error
	connected := false
	for _, addr := range addrs {
		var ip net.IP
		switch v := addr.(type) {
		case *net.IPNet:
			ip = v.IP
		case *net.IPAddr:
			ip = v.IP
		}
		if ip == nil {
			continue
		}
		// If this is an ipv6 address and we're not connecting to ipv6, skip.
		if ip.To4() == nil && !ipv6 {
			continue
		}
		// If this is an ipv4 address and we're connecting to ipv6, skip.
		if ip.To4() != nil && ipv6 {
			continue
		}
		if err := c.ConnectTo(port, ip, iface); err != nil {
			lastConnectErr = err
			log.Println(lastConnectErr)
			continue
		}
		connected = true
		break
	}
	// This means there were no errors attempting to connect to a valid address,
	// only that no candidate addrs were found.
	if lastConnectErr == nil {
		return connected, nil
	}
	return connected, fmt.Errorf("unable to connect to any addr. last err: %w", lastConnectErr)
}

// connectOnAllIfaces is a helper function that takes an mDNSConn and attempts
// to connect on the first available address of all accessible interfaces.
func connectOnAllIfaces(c mDNSConn, ifaces []net.Interface, port int, ipv6 bool) error {
	var lastConnectErr error
	connected := false
	for _, iface := range ifaces {
		if iface.Flags&net.FlagMulticast == 0 || iface.Flags&net.FlagUp == 0 {
			continue
		}
		if err := c.JoinGroup(iface); err != nil {
			lastConnectErr = fmt.Errorf("joining %v: %w", iface, err)
			log.Println(lastConnectErr)
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			lastConnectErr = err
			continue
		}
		if len(addrs) == 0 {
			continue
		}
		c, err := connectToAnyAddr(c, &iface, addrs, port, ipv6)
		if err != nil {
			lastConnectErr = err
		}
		connected = connected || c
	}
	if !connected {
		return fmt.Errorf("unable to connect to any iface. last error: %w", lastConnectErr)
	}
	return nil
}

func (m *mDNS) initMDNSConns(port int) error {
	if m.conn4 == nil && m.conn6 == nil {
		return fmt.Errorf("no connections active")
	}
	if m.conn4 != nil {
		if err := m.conn4.InitReceiver(port); err != nil {
			return err
		}
	}
	if m.conn6 != nil {
		if err := m.conn6.InitReceiver(port); err != nil {
			return err
		}
	}
	ifaces, err := net.Interfaces()
	if err != nil {
		return fmt.Errorf("listing interfaces: %w", err)
	}
	var v4Err error
	var v6Err error
	if m.conn4 != nil {
		v4Err = connectOnAllIfaces(m.conn4, ifaces, port, false)
		if v4Err != nil {
			m.conn4.Close()
			m.conn4 = nil
			// If this is the only available mDNSConn just return the err.
			if m.conn6 == nil {
				return v4Err
			}
		}
	}
	if m.conn6 != nil {
		v6Err = connectOnAllIfaces(m.conn6, ifaces, port, true)
		if v6Err != nil {
			m.conn6.Close()
			m.conn6 = nil
			// If this is the only available mDNSConn just return the err.
			if m.conn4 == nil {
				return v6Err
			}
		}
	}
	// If we've made it here, at least one connection should have succeeded,
	// else both mDNSConn's attempted to connect and failed.
	if v4Err != nil && v6Err != nil {
		return fmt.Errorf("mdns conn errors (v4, v6): %q, %q", v4Err, v6Err)
	}
	return nil
}

// Start causes m to start listening for MDNS packets on all interfaces on
// the specified port. Listening will stop if ctx is done.
func (m *mDNS) Start(ctx context.Context, port int) error {
	if err := m.initMDNSConns(port); err != nil {
		m.Close()
		return err
	}
	go func() {
		// NOTE: This defer statement will close connections, which will force
		// the goroutines started by startListenLoop() to exit.
		defer m.Close()

		var chan4 <-chan receivedPacketInfo
		var chan6 <-chan receivedPacketInfo

		if m.conn4 != nil {
			chan4 = m.conn4.Listen()
		}
		if m.conn6 != nil {
			chan6 = m.conn6.Listen()
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
				go p(*received.iface, received.src, packet)
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
