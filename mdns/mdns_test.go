// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mdns

import (
	"bytes"
	"testing"
)

func TestUint16(t *testing.T) {
	var buf bytes.Buffer
	v := uint16(6857)
	writeUint16(&buf, v)
	var v2 uint16
	readUint16(&buf, &v2)
	if v != v2 {
		t.Errorf("read/writeUint16 mismatch: wrote %v, read %v", v, v2)
	}
}

func TestUint32(t *testing.T) {
	var buf bytes.Buffer
	v := uint32(6857)
	writeUint32(&buf, v)
	var v2 uint32
	readUint32(&buf, &v2)
	if v != v2 {
		t.Errorf("read/writeUint32 mismatch: wrote %v, read %v", v, v2)
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
	if v != v2 {
		t.Errorf("header (de)serialize mismatch: wrote %v, read %v", v, v2)
	}
}

func TestDomain(t *testing.T) {
	var buf bytes.Buffer
	v := "this.is.a.random.domain.to.check"
	writeDomain(&buf, v)
	var v2 string
	readDomain(buf.Bytes(), &buf, &v2)
	if v != v2 {
		t.Errorf("read/writeDomain mismatch: wrote %v, read %v", v, v2)
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
	if v != v2 {
		t.Errorf("question (de)serialize mismatch: wrote %v, read %v", v, v2)
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
	if v.Domain != v2.Domain {
		t.Errorf("record (de)serialize mismatch (domain): wrote %v, read %v", v.Domain, v2.Domain)
	}
	if v.Type != v2.Type {
		t.Errorf("record (de)serialize mismatch (type): wrote %v, read %v", v.Type, v2.Type)
	}
	if v.Class != v2.Class {
		t.Errorf("record (de)serialize mismatch (class): wrote %v, read %v", v.Class, v2.Class)
	}
	if v.Flush != v2.Flush {
		t.Errorf("record (de)serialize mismatch (flush): wrote %v, read %v", v.Flush, v2.Flush)
	}
	if v.TTL != v2.TTL {
		t.Errorf("record (de)serialize mismatch (ttl): wrote %v, read %v", v.TTL, v2.TTL)
	}
	if !equalBytes(v.Data, v2.Data) {
		t.Errorf("record (de)serialize mismatch (data): wrote %v, read %v", v.Data, v2.Data)
	}
}
