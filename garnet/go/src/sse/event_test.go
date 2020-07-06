// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sse

import (
	"bytes"
	"testing"
)

type marshalTestCase struct {
	event Event
	want  string
}

var positive = 1
var negative = -1

var marshalCases = []marshalTestCase{
	{Event{Event: "foo", Data: []byte(`["foo"]`)}, "event: foo\ndata: [\"foo\"]\n\n"},
	{Event{Event: "bar", Data: []byte(`{"foo":"bar"}`)}, "event: bar\ndata: {\"foo\":\"bar\"}\n\n"},
	{Event{Data: []byte(`["foo"]`)}, "data: [\"foo\"]\n\n"},
	{Event{ID: "foo", Data: []byte(`[]`)}, "id: foo\ndata: []\n\n"},
	{Event{Data: []byte(`[]`), Retry: &positive}, "retry: 1\ndata: []\n\n"},
	{Event{Data: []byte(`[]`), Retry: &negative}, "data: []\n\n"},
	{Event{ID: "1", Event: "2", Data: []byte(`[3]`), Retry: &positive}, "event: 2\nid: 1\nretry: 1\ndata: [3]\n\n"},
}

func TestEventMarshal(t *testing.T) {
	for _, tc := range marshalCases {
		g := tc.event.Marshal()
		if string(g) != tc.want {
			t.Errorf("got %q, want %q", g, tc.want)
		}
	}
}

func TestEventString(t *testing.T) {
	e := &Event{Event: "test", Data: []byte(`{"test": "value"}`)}
	w := "event: test\ndata: " + `{"test": "value"}` + "\n\n"
	g := e.String()
	if g != w {
		t.Errorf("got %q, want %q", g, w)
	}
}

func TestEventWriteTo(t *testing.T) {
	b := &bytes.Buffer{}
	e := &Event{Event: "test", Data: []byte(`{"test": "value"}`)}
	e.WriteTo(b)
	if !bytes.Equal(b.Bytes(), e.Marshal()) {
		t.Fatalf("expected %q, got %q", e.Marshal(), b.Bytes())
	}
}
