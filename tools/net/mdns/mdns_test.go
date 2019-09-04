// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mdns

import (
	"bytes"
	"net"
	"testing"

	"github.com/google/go-cmp/cmp"
)

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
