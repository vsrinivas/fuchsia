// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mdns

import (
	"bytes"
	"context"
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
	ifi      *net.Interface
}

func (f *fakePacketConn) MulticastInterface() (*net.Interface, error) {
	return f.ifi, nil
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

func (f *fakeNetFactory) MakeUDPSocket(ifi *net.Interface, _ *net.UDPAddr, ttl int) (packetConn, error) {
	if ifi.Name == fakeIfaceMCastUpConnectToError.Name {
		return nil, fmt.Errorf("returning testing error for iface %q", ifi.Name)
	}
	f.ttlCh <- ttl
	return &fakePacketConn{packetCh: f.packetCh, ifi: ifi}, nil
}

// Returns a fake mDNS connection, a multicast packet channel, a unicast
// packet channel, and a TTL-to-be-sent channel respectively. The latter does
// not return any data. It is to indicate the TTL that was set for all packets
// on this connection.
//
// Keep in mind if running multiple ConnectTo's it will simply add multiple
// readers to ONE channel. This doesn't support something like mapping ports to
// specific channels or the like.
func makeFakeMDNSConn(v6 bool) (mDNSConnBase, chan []byte, chan int) {
	// NOTE: In the event that some sort of mapping is going to be made or
	// multiple values intend to be pushed into these channels, then this
	// implementation will need to be updated.
	in := make(chan []byte, 1)
	ttl := make(chan int, 1)
	// TODO(awdavies): Should this be a dep injection from a function rather
	// than constructed purely for tests?
	c := mDNSConnBase{
		netFactory: &fakeNetFactory{packetCh: in, ttlCh: ttl},
		ttl:        -1,
	}
	if v6 {
		c.dst.IP = defaultMDNSMulticastIPv6
	} else {
		c.dst.IP = defaultMDNSMulticastIPv4
	}
	return c, in, ttl
}

func TestCast4(t *testing.T) {
	c, in, ttl := makeFakeMDNSConn(false)
	defer c.Close()
	c.dst.IP = net.ParseIP("192.168.2.2")
	c.dst.Port = 12345
	if err := c.JoinGroup(fakeIfaceMCastUp); err != nil {
		t.Fatalf("Unable to connect to port: %s", err)
	}
	ttlWant := -1
	ttlGot := <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Fatalf("ttl for senders incorrect (-want +got)\n%s", d)
	}
	if err := c.JoinGroup(fakeIfaceMCastUp); err != nil {
		t.Fatalf("Unable to connect to port: %s", err)
	}
	ttlGot = <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}

	rpChan := c.Listen(context.Background())

	want := []byte{0, 1, 2, 3, 4}
	// "Write" packet to the unicast listeners.
	in <- want
	got := <-rpChan
	// Compare a subsection, as this will by default return a kilobyte buffer.
	if d := cmp.Diff(want, got.data[:len(want)]); d != "" {
		t.Errorf("read/write mismatch (-wrote +read)\n%s", d)
	}
}

func TestTTLValues(t *testing.T) {
	c, _, _ := makeFakeMDNSConn(false)
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

func TestCast6(t *testing.T) {
	c, in, ttl := makeFakeMDNSConn(true)
	defer c.Close()
	c.dst.IP = net.ParseIP("2001:db8::1")
	c.dst.Port = 1234
	if err := c.JoinGroup(fakeIfaceMCastUp); err != nil {
		t.Fatalf("Unable to connect to port: %s", err)
	}
	ttlWant := -1
	ttlGot := <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}

	c.SetMCastTTL(0)
	if err := c.JoinGroup(fakeIfaceMCastUp); err != nil {
		t.Fatalf("Unable to connect to port: %s", err)
	}
	ttlWant = 0
	ttlGot = <-ttl
	if d := cmp.Diff(ttlWant, ttlGot); d != "" {
		t.Errorf("ttl for senders incorrect (-want +got)\n%s", d)
	}

	rpChan := c.Listen(context.Background())

	want := []byte{0, 9, 2, 9, 2}
	// "Write" packets to the listeners.
	in <- want
	got := <-rpChan
	// Compare a subsection, as this will by default return a kilobyte buffer.
	if d := cmp.Diff(want, got.data[:len(want)]); d != "" {
		t.Errorf("read/write mismatch (-wrote +read)\n%s", d)
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
	got := m.conn4.dst.IP
	// Should send to the default address.
	want := net.ParseIP("224.0.0.251")
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("IP mismatch (-want +got)\n%s", d)
	}

	if err := m.SetAddress("11.22.33.44"); err != nil {
		t.Errorf("SetAddress() returned error: %s", err)
	} else {
		got = m.conn4.dst.IP
		// Should send to the given custom address.
		want = net.ParseIP("11.22.33.44")
		if d := cmp.Diff(want, got); d != "" {
			t.Errorf("IP mismatch (-want +got)\n%s", d)
		}
	}

	m = NewMDNS()
	m.EnableIPv6()
	got = m.conn6.dst.IP
	want = net.ParseIP("ff02::fb")
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("IP mismatch (-want +got)\n%s", d)
	}

	if err := m.SetAddress("11:22::33:44"); err != nil {
		t.Errorf("SetAddress() returned error: %s", err)
	} else {
		got = m.conn6.dst.IP
		// Should send to the given custom address.
		want = net.ParseIP("11:22::33:44")
		if d := cmp.Diff(want, got); d != "" {
			t.Errorf("IP mismatch (-want +got)\n%s", d)
		}
	}
}
