// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sse

import (
	"bytes"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

func ExampleStart() {
	http.HandleFunc("/example/startsse", func(w http.ResponseWriter, r *http.Request) {
		err := Start(w, r)
		if err != nil {
			w.WriteHeader(http.StatusNotFound)
			return
		}
		ticker := time.NewTicker(time.Second)
		for {
			select {
			case <-ticker.C:
				helloWorld := &Event{Data: []byte("hello world")}
				helloWorld.WriteTo(w)
			case <-r.Context().Done():
				ticker.Stop()
				return
			}
		}
	})
}

func TestStart_ErrNotAcceptable(t *testing.T) {
	w := httptest.NewRecorder()
	r, _ := http.NewRequest("GET", "/", nil)
	err := Start(w, r)
	if err != ErrNotAcceptable {
		t.Fatalf("expected ErrNotAcceptable, got %#v", err)
	}

	w = httptest.NewRecorder()
	r.Header.Set("Accept", "text/event-stream")
	err = Start(w, r)
	if err != nil {
		t.Fatalf("unexpected error: %#v", err)
	}
}

func TestStart_SetsHeaders(t *testing.T) {
	w := httptest.NewRecorder()
	r, _ := http.NewRequest("GET", "/", nil)
	r.Header.Set("Accept", "text/event-stream")
	err := Start(w, r)
	if err != nil {
		t.Fatalf("unexpected error: %#v", err)
	}

	var headers = map[string]string{
		"Content-Type":      "text/event-stream",
		"Cache-Control":     "no-cache",
		"X-Accel-Buffering": "no",
	}
	for k, v := range headers {
		g := w.Header().Get(k)
		if g != v {
			t.Errorf("expected %s to be %q, got %q", k, v, g)
		}
	}
}

func TestStart_Writes200(t *testing.T) {
	w := httptest.NewRecorder()
	r, _ := http.NewRequest("GET", "/", nil)
	r.Header.Set("Accept", "text/event-stream")
	err := Start(w, r)
	if err != nil {
		t.Fatalf("unexpected error: %#v", err)
	}
	if w.Code != 200 {
		t.Fatalf("expected 200, got %d", w.Code)
	}
}

func TestStart_Flushed(t *testing.T) {
	w := httptest.NewRecorder()
	r, _ := http.NewRequest("GET", "/", nil)
	r.Header.Set("Accept", "text/event-stream")
	err := Start(w, r)
	if err != nil {
		t.Fatalf("unexpected error: %#v", err)
	}
	if !w.Flushed {
		t.Fatal("expected connection to be flushed")
	}
}

func TestWrite(t *testing.T) {
	w := httptest.NewRecorder()
	r, _ := http.NewRequest("GET", "/", nil)
	r.Header.Set("Accept", "text/event-stream")
	err := Start(w, r)
	if err != nil {
		t.Fatalf("unexpected error: %#v", err)
	}
	if !w.Flushed {
		t.Fatal("expected connection to be flushed")
	}
	w.Flushed = false
	Write(w, &Event{Data: []byte("test")})
	if !w.Flushed {
		t.Fatal("Write failed to flush")
	}
	if w.Body.String() != "data: test\n\n" {
		t.Fatalf("expected %q, got %q", "data: test\n\n", w.Body.String())
	}
}

func TestClientServer(t *testing.T) {
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		err := Start(w, r)
		if err != nil {
			w.WriteHeader(http.StatusNotFound)
			return
		}
		Write(w, &Event{Data: []byte("hello world")})
	}))
	defer s.Close()
	defer s.CloseClientConnections()

	req, err := http.NewRequest("GET", s.URL, nil)
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Accept", "text/event-stream")
	res, err := s.Client().Do(req)
	if err != nil {
		t.Fatal(err)
	}
	cli, err := New(res)
	if err != nil {
		t.Fatal(err)
	}
	e, err := cli.ReadEvent()
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(e.Data, []byte("hello world")) {
		t.Fatal("unexpected event data")
	}
}
