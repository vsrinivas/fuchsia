// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mdns

import (
	"bytes"
	"testing"
)

func testUint16(t *testing.T) {
	var buf bytes.Buffer
	v := uint16(6857)
	writeUint16(&buf, v)
	var v2 uint16
	readUint16(&buf, &v2)
	if v != v2 {
		t.Fatal()
	}
}

func testUint32(t *testing.T) {
	var buf bytes.Buffer
	v := uint32(6857)
	writeUint32(&buf, v)
	var v2 uint32
	readUint32(&buf, &v2)
	if v != v2 {
		t.Fatal()
	}
}

func testHeader(t *testing.T) {
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
	v.deserialize(buf.Bytes(), &buf)
	if v != v2 {
		t.Fatal()
	}
}

func testDomain(t *testing.T) {
	var buf bytes.Buffer
	v := "this.is.a.random.domain.to.check"
	writeDomain(&buf, v)
	var v2 string
	readDomain(buf.Bytes(), &buf, &v2)
	if v != v2 {
		t.Fatal()
	}
}

func testQuestion(t *testing.T) {
	var buf bytes.Buffer
	v := Question{
		Domain:  "some.random.thing.local",
		Type:    5954,
		Unicast: true,
	}
	v.serialize(&buf)
	var v2 Question
	v2.deserialize(buf.Bytes(), &buf)
	if v != v2 {
		t.Fatal()
	}
}

func equalBytes(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i, ai := range a {
		if ai != b[i] {
			return false
		}
	}
	return true
}

func testRecord(t *testing.T) {
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
	if v.Domain != v2.Domain {
		t.Fatal()
	}
	if v.Type != v2.Type {
		t.Fatal()
	}
	if v.Class != v2.Class {
		t.Fatal()
	}
	if v.Flush != v2.Flush {
		t.Fatal()
	}
	if v.TTL != v2.TTL {
		t.Fatal()
	}
	if !equalBytes(v.Data, v2.Data) {
		t.Fatal()
	}
}
