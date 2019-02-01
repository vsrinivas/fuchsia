// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sse

import (
	"bytes"
	"io/ioutil"
	"net/http"
	"testing"
)

func TestClient_ReadEvent(t *testing.T) {
	body := ioutil.NopCloser(bytes.NewReader([]byte("data: example\n\n")))
	c, err := New(&http.Response{
		Header:        http.Header{"Content-Type": []string{"text/event-stream"}},
		ContentLength: -1,
		Body:          body,
	})

	if err != nil {
		t.Fatal(err)
	}

	if c == nil {
		t.Fatal("expected client, got nil")
	}

	m, err := c.ReadEvent()
	if err != nil {
		t.Fatal(err)
	}
	if got, want := string(m.Data), "example"; got != want {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestNew(t *testing.T) {
	c, err := New(&http.Response{
		Header:        http.Header{"Content-Type": []string{"text/event-stream"}},
		ContentLength: -1,
	})

	if err != nil {
		t.Fatal(err)
	}

	if c == nil {
		t.Fatal("expected client, got nil")
	}
}

func TestNew_BadContentType(t *testing.T) {
	r := &http.Response{
		Header:        http.Header{"Content-Type": []string{"application/json"}},
		ContentLength: -1,
	}

	_, err := New(r)

	if _, ok := err.(*ProtocolError); !ok {
		t.Fatalf("expected ProtocolError, got %#v", err)
	}

}
func TestNew_EmptyBody(t *testing.T) {
	_, err := New(&http.Response{ContentLength: 0})

	if _, ok := err.(*ProtocolError); !ok {
		t.Fatalf("expected ProtocolError, got %#v", err)
	}

	_, err = New(&http.Response{ContentLength: 1})

	if _, ok := err.(*ProtocolError); !ok {
		t.Fatalf("expected ProtocolError, got %#v", err)
	}
}
