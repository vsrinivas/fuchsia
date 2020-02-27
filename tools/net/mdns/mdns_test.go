// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mdns

import (
	"bytes"
	"fmt"
	"net"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
)

var (
	fakeIfaceMCastUp = &net.Interface{
		Index:        1,
		MTU:          1800,
		Name:         "faketh0",
		HardwareAddr: []byte{0, 0, 0, 0, 0, 0},
		Flags:        net.FlagMulticast | net.FlagUp,
	}
	fakeIfaceMCastUpJoinGroupError = &net.Interface{
		Index:        2,
		MTU:          1800,
		Name:         "faketh1",
		HardwareAddr: []byte{0x2, 0, 0, 0, 0, 0},
		Flags:        net.FlagMulticast | net.FlagUp,
	}
	fakeIfaceMCastUpConnectToError = &net.Interface{
		Index:        3,
		MTU:          1800,
		Name:         "faketh3",
		HardwareAddr: []byte{0x8, 0, 0, 0, 0, 0},
		Flags:        net.FlagMulticast | net.FlagUp,
	}
	fakeIfaceMCastDown = &net.Interface{
		Index:        4,
		MTU:          1800,
		Name:         "faketh4",
		HardwareAddr: []byte{0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
		Flags:        net.FlagMulticast,
	}
)

type fakeAddr struct{}

func (f *fakeAddr) Network() string {
	return "udp"
}

func (f *fakeAddr) String() string {
	return "192.168.2.2"
}

type fakePacketConn struct {
	packetCh chan []byte
}

func (f *fakePacketConn) ReadFrom(p []byte) (n int, addr net.Addr, err error) {
	buf := <-f.packetCh
	for i, b := range buf {
		p[i] = b
	}
	n = len(buf)
	addr = &fakeAddr{}
	err = nil
	return
}

func (f *fakePacketConn) WriteTo(p []byte, addr net.Addr) (n int, err error) {
	f.packetCh <- p
	n = len(p)
	err = nil
	return
}

func (f *fakePacketConn) Close() error {
	return nil
}

func (f *fakePacketConn) LocalAddr() net.Addr {
	return &fakeAddr{}
}

func (f *fakePacketConn) SetDeadline(t time.Time) error {
	return nil
}

func (f *fakePacketConn) SetReadDeadline(t time.Time) error {
	return nil
}

func (f *fakePacketConn) SetWriteDeadline(t time.Time) error {
	return nil
}

type fakeNetFactory struct {
	packetCh chan []byte
	ttlCh    chan int
}

func (f *fakeNetFactory) MakeUDPSocket(port, ttl int, ip net.IP, iface *net.Interface) (net.PacketConn, error) {
	if iface.Name == fakeIfaceMCastUpConnectToError.Name {
		return nil, fmt.Errorf("returning testing error for iface %q", iface.Name)
	}
	f.ttlCh <- ttl
	return &fakePacketConn{packetCh: f.packetCh}, nil
}

func (f *fakeNetFactory) MakePacketReceiver(conn net.PacketConn, ipv6 bool) packetReceiver {
	return &fakePacketReceiver{packetConn: conn}
}

type fakePacketReceiver struct {
	packetConn net.PacketConn
}

func (f *fakePacketReceiver) ReadPacket(buf []byte) (size int, iface *net.Interface, src net.Addr, err error) {
	size, _, err = f.packetConn.ReadFrom(buf)
	src = &fakeAddr{}
	iface = fakeIfaceMCastUp
	err = nil
	return
}

func (f *fakePacketReceiver) JoinGroup(i *net.Interface, n net.Addr) error {
	if i.Name == fakeIfaceMCastUpJoinGroupError.Name {
		return fmt.Errorf("returning testing error for iface %q", i.Name)
	}
	return nil
}

func (f *fakePacketReceiver) Close() error {
	return nil
}

// Returns a fake mDNS connection, a multicast packet channel, a unicast
// packet channel, and a TTL-to-be-sent channel respectively. The latter does
// not return any data. It is to indicate the TTL that was set for all packets
// on this connection.
//
// Keep in mind if running multiple ConnectTo's it will simply add multiple
// readers to ONE channel. This doesn't support something like mapping ports to
// specific channels or the like.
func makeFakeMDNSConn(v6 bool) (mDNSConn, chan []byte, chan []byte, chan int) {
	// NOTE: In the event that some sort of mapping is going to be made or
	// multiple values intend to be pushed into these channels, then this
	// implementation will need to be updated.
	ucast := make(chan []byte, 1)
	mcast := make(chan []byte, 1)
	ttl := make(chan int, 1)
	if v6 {
		// TODO(awdavies): Should this be a dep injection from a function rather
		// than constructed purely for tests?
		c := &mDNSConn6{mDNSConnBase{
			netFactory: &fakeNetFactory{packetCh: ucast, ttlCh: ttl},
			ttl:        -1,
		}}
		// This needs to be initialized, which happens in the |InitReceiver|
		// function inside of the MDNS |Start| function.
		c.receiver = &fakePacketReceiver{
			packetConn: &fakePacketConn{packetCh: mcast},
		}
		c.SetIp(defaultMDNSMulticastIPv6)
		return c, mcast, ucast, ttl
	} else {
		c := &mDNSConn4{mDNSConnBase{
			netFactory: &fakeNetFactory{packetCh: ucast, ttlCh: ttl},
			ttl:        -1,
		}}
		c.receiver = &fakePacketReceiver{
			packetConn: &fakePacketConn{packetCh: mcast},
		}
		c.SetIp(defaultMDNSMulticastIPv4)
		return c, mcast, ucast, ttl
	}
}

func TestConnectToAnyAddr(t *testing.T) {
	iface := *fakeIfaceMCastUp
	for _, test := range []struct {
		name            string
		addrs           []net.Addr
		expectConnected bool
		ipv6            bool
	}{
		{
			name:            "ipv6 only expect ipv4",
			ipv6:            false,
			expectConnected: false,
			addrs: []net.Addr{
				&net.IPAddr{IP: net.ParseIP("fe80::1234:1234:1234:1234")},
				&net.IPAddr{IP: net.ParseIP("fe80::1456:1456:1456:1456")},
				&net.IPAddr{IP: net.ParseIP("fe80::6789:6789:6789:6789")},
			},
		},
		{
			name:            "ipv4 only expect ipv6",
			ipv6:            true,
			expectConnected: false,
			addrs: []net.Addr{
				&net.IPAddr{IP: net.ParseIP("127.1.1.2")},
				&net.IPAddr{IP: net.ParseIP("128.1.1.2")},
				&net.IPAddr{IP: net.ParseIP("129.1.1.2")},
			},
		},
		{
			name:            "ipv4 only expect ipv4",
			ipv6:            false,
			expectConnected: true,
			addrs: []net.Addr{
				&net.IPAddr{IP: net.ParseIP("127.1.1.2")},
				&net.IPAddr{IP: net.ParseIP("128.1.1.2")},
				&net.IPAddr{IP: net.ParseIP("129.1.1.2")},
			},
		},
		{
			name:            "ipv6 only expect ipv6",
			ipv6:            true,
			expectConnected: true,
			addrs: []net.Addr{
				&net.IPAddr{IP: net.ParseIP("fe80::1456:1456:1456:1456")},
				&net.IPAddr{IP: net.ParseIP("fe80::6789:6789:6789:6789")},
			},
		},
	} {
		t.Run(test.name, func(t *testing.T) {
			c, _, _, _ := makeFakeMDNSConn(test.ipv6)
			defer c.Close()
			connected, err := connectToAnyAddr(c, &iface, test.addrs, 1234, test.ipv6)
			if err != nil {
				t.Fatal(err)
			}

			if connected != test.expectConnected {
				t.Errorf("connection invalid: want %t, got %t", test.expectConnected, connected)
			}
		})
	}
}

func TestConnectToAnyIface_noValidConnections(t *testing.T) {
	ifaces := []net.Interface{
		*fakeIfaceMCastDown,
		*fakeIfaceMCastUpJoinGroupError,
		*fakeIfaceMCastUpConnectToError,
		*fakeIfaceMCastDown,
	}
	c, _, _, _ := makeFakeMDNSConn(false)
	defer c.Close()
	if err := connectOnAllIfaces(c, ifaces, 1234, false); err == nil {
		t.Errorf("expected err. func successful")
	}
}

func TestConnectToAnyIface_oneValidConnection(t *testing.T) {
	ifaces := []net.Interface{
		*fakeIfaceMCastDown,
		*fakeIfaceMCastUpJoinGroupError,
		*fakeIfaceMCastUpConnectToError,
		*fakeIfaceMCastUp,
		*fakeIfaceMCastDown,
	}
	c, _, _, _ := makeFakeMDNSConn(false)
	defer c.Close()
	if err := connectOnAllIfaces(c, ifaces, 1234, false); err != nil {
		t.Errorf("expected successful connection, received err: %v", err)
	}
}

func TestUCast4(t *testing.T) {
	c, _, ucast, ttl := makeFakeMDNSConn(false)
	defer c.Close()
	c.SetAcceptUnicastResponses(true)
	ip := net.ParseIP("192.168.2.2")
	if err := c.ConnectTo(12345, ip, fakeIfaceMCastUp); err != nil {
		t.Errorf("Unable to connect to port: %v", err)
		return
	}
	ttlWant := -1
	ttlGot := <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}
	if err := c.ConnectTo(12345, ip, fakeIfaceMCastUp); err != nil {
		t.Errorf("Unable to connect to port: %v", err)
		return
	}
	ttlGot = <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}

	rpChan := c.Listen()

	want := []byte{0, 1, 2, 3, 4}
	// "Write" packet to the unicast listeners.
	ucast <- want
	got := <-rpChan
	// Compare a subsection, as this will by default return a kilobyte buffer.
	if d := cmp.Diff(want, got.data[:len(want)]); d != "" {
		t.Errorf("read/write ucast mismatch (-wrote +read)\n%s", d)
	}
}

func TestTTLValues(t *testing.T) {
	c, _, _, _ := makeFakeMDNSConn(false)
	defer c.Close()
	// Expecting errors.
	if err := c.SetMCastTTL(256); err == nil {
		t.Errorf("expecting error SetMCastTTL()")
	}
	if err := c.SetMCastTTL(10000); err == nil {
		t.Errorf("expecting error SetMCastTTL()")
	}

	// Expecting no errors.
	if err := c.SetMCastTTL(-10); err != nil {
		t.Errorf("expecting no error SetMCastTTL()")
	}
}

func TestUCast6(t *testing.T) {
	c, _, ucast, ttl := makeFakeMDNSConn(true)
	defer c.Close()
	c.SetAcceptUnicastResponses(true)
	ip := net.ParseIP("2001:db8::1")
	if err := c.ConnectTo(1234, ip, fakeIfaceMCastUp); err != nil {
		t.Errorf("Unable to connect to port: %v", err)
		return
	}
	ttlWant := -1
	ttlGot := <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}

	c.SetMCastTTL(0)
	if err := c.ConnectTo(12345, ip, fakeIfaceMCastUp); err != nil {
		t.Errorf("Unable to connect to port: %v", err)
		return
	}
	ttlWant = 0
	ttlGot = <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}

	rpChan := c.Listen()

	want := []byte{0, 9, 2, 9, 2}
	// "Write" packets to the listeners.
	ucast <- want
	got := <-rpChan
	// Compare a subsection, as this will by default return a kilobyte buffer.
	if d := cmp.Diff(want, got.data[:len(want)]); d != "" {
		t.Errorf("read/write ucast mismatch (-wrote +read)\n%s", d)
	}
}

func TestMCast6(t *testing.T) {
	c, mcast, ucast, ttl := makeFakeMDNSConn(true)
	defer c.Close()
	ip := net.ParseIP("2001:db8::1")
	if err := c.ConnectTo(1234, ip, fakeIfaceMCastUp); err != nil {
		t.Errorf("Unable to connect to port: %v", err)
		return
	}

	rpChan := c.Listen()

	doNotWant := []byte{1, 2, 3, 1, 2, 3, 4, 5}
	want := []byte{0, 9, 2, 9, 2}
	// "Write" packets to the listeners. Ucast should always be ignored since
	// accepting unicast hasn't been set to true.
	ucast <- doNotWant
	mcast <- want
	got := <-rpChan
	// Compare a subsection, as this will by default return a kilobyte buffer.
	if d := cmp.Diff(want, got.data[:len(want)]); d != "" {
		t.Errorf("read/write ucast mismatch (-wrote +read)\n%s", d)
	}
	ttlWant := -1
	ttlGot := <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}
}

func TestMCast4(t *testing.T) {
	c, mcast, ucast, ttl := makeFakeMDNSConn(false)
	defer c.Close()
	ip := net.ParseIP("192.168.10.10")
	ttlWant := 23
	c.SetMCastTTL(ttlWant)
	if err := c.ConnectTo(1234, ip, fakeIfaceMCastUp); err != nil {
		t.Errorf("Unable to connect to port: %v", err)
		return
	}
	ttlGot := <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}

	rpChan := c.Listen()

	doNotWant := []byte{1, 2, 3, 1, 2, 3, 4, 5}
	want := []byte{0, 9, 2, 9, 2}
	// "Write" packets to the listeners. Ucast should always be ignored since
	// accepting unicast hasn't been set to true.
	ucast <- doNotWant
	mcast <- want
	got := <-rpChan
	// Compare a subsection, as this will by default return a kilobyte buffer.
	if d := cmp.Diff(want, got.data[:len(want)]); d != "" {
		t.Errorf("read/write ucast mismatch (-wrote +read)\n%s", d)
	}
}

func TestConnectToRejectedMDNS4(t *testing.T) {
	c, _, _, _ := makeFakeMDNSConn(false)
	defer c.Close()
	ip := net.ParseIP("2001:db8::1")
	if err := c.ConnectTo(12345, ip, fakeIfaceMCastUp); err == nil {
		t.Errorf("ConnectTo expected error when connecting to IPv6 addr")
	}
}

func TestUint16(t *testing.T) {
	var buf bytes.Buffer
	v := uint16(6857)
	writeUint16(&buf, v)
	var v2 uint16
	readUint16(&buf, &v2)
	if d := cmp.Diff(v, v2); d != "" {
		t.Errorf("read/writeUint16: mismatch (-wrote +read)\n%s", d)
	}
}

func TestUint32(t *testing.T) {
	var buf bytes.Buffer
	v := uint32(6857)
	writeUint32(&buf, v)
	var v2 uint32
	readUint32(&buf, &v2)
	if d := cmp.Diff(v, v2); d != "" {
		t.Errorf("read/writeUint32: mismatch (-wrote +read)\n%s", d)
	}
}

func TestHeader(t *testing.T) {
	var buf bytes.Buffer
	v := Header{
		ID:      593,
		Flags:   795,
		QDCount: 5839,
		ANCount: 9009,
		NSCount: 8583,
		ARCount: 7764,
	}
	v.serialize(&buf)
	var v2 Header
	v2.deserialize(buf.Bytes(), &buf)
	if d := cmp.Diff(v, v2); d != "" {
		t.Errorf("header (de)serialize: mismatch (-serialize +deserialize)\n%s", d)
	}
}

func TestDomain(t *testing.T) {
	var buf bytes.Buffer
	v := "this.is.a.random.domain.to.check"
	writeDomain(&buf, v)
	var v2 string
	readDomain(buf.Bytes(), &buf, &v2)
	if d := cmp.Diff(v, v2); d != "" {
		t.Errorf("read/writeDomain: mismatch (-wrote +read)\n%s", d)
	}
}

func TestQuestion(t *testing.T) {
	var buf bytes.Buffer
	v := Question{
		Domain:  "some.random.thing.local",
		Type:    5954,
		Unicast: true,
	}
	v.serialize(&buf)
	var v2 Question
	v2.deserialize(buf.Bytes(), &buf)
	if d := cmp.Diff(v, v2); d != "" {
		t.Errorf("question (de)serialize: mismatch (-serialize +deserialize)\n%s", d)
	}
}

func TestRecord(t *testing.T) {
	var buf bytes.Buffer
	v := Record{
		Domain: "some.random.thing",
		Type:   1234,
		Class:  8765,
		Flush:  true,
		TTL:    18656,
		Data:   []byte{45, 145, 253, 167, 34, 74},
	}
	v.serialize(&buf)
	var v2 Record
	v2.deserialize(buf.Bytes(), &buf)
	if d := cmp.Diff(v, v2); d != "" {
		t.Errorf("record (de)serialize: mismatch (-serialize +deserialize)\n%s", d)
	}
}

func TestIpToDnsRecordType(t *testing.T) {
	ip1 := "224.0.0.251"
	if addrType := IpToDnsRecordType(net.ParseIP(ip1)); addrType != A {
		t.Errorf("IpToDnsRecordType(%s) mismatch %v, wanted %v", ip1, addrType, A)
	}
	ip2 := "ff2e::fb"
	if addrType := IpToDnsRecordType(net.ParseIP(ip2)); addrType != AAAA {
		t.Errorf("IpToDnsRecordType(%s) mismatch %v, wanted %v", ip2, addrType, AAAA)
	}
}

func TestSetAddress(t *testing.T) {
	m := NewMDNS()
	m.EnableIPv4()
	got := m.ipToSend()
	// Should send to the default address.
	want := net.ParseIP("224.0.0.251")
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("ipToSend (default): mismatch (-want +got)\n%s", d)
	}

	if err := m.SetAddress("11.22.33.44"); err != nil {
		t.Errorf("SetAddress() returned error: %s", err)
	} else {
		got = m.ipToSend()
		// Should send to the given custom address.
		want = net.ParseIP("11.22.33.44")
		if d := cmp.Diff(want, got); d != "" {
			t.Errorf("ipToSend (custom): mismatch (-want +got)\n%s", d)
		}
	}

	m = NewMDNS()
	m.EnableIPv6()
	got = m.ipToSend()
	want = net.ParseIP("ff02::fb")
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("ipToSend (default): mismatch (-want +got)\n%s", d)
	}

	if err := m.SetAddress("11:22::33:44"); err != nil {
		t.Errorf("SetAddress() returned error: %s", err)
	} else {
		got = m.ipToSend()
		// Should send to the given custom address.
		want = net.ParseIP("11:22::33:44")
		if d := cmp.Diff(want, got); d != "" {
			t.Errorf("ipToSend (custom): mismatch (-want +got)\n%s", d)
		}
	}
}
