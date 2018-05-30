// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"syscall/zx"
	"testing"
	"time"

	. "fidl/bindings"
	. "fidl/fidl/test/gobindings"
)

type Test1Impl struct{}

func (t *Test1Impl) Echo(s *string) (*string, error) {
	return s, nil
}

func TestEcho(t *testing.T) {
	t.Parallel()

	ch, sh, err := zx.NewChannel(0)
	if err != nil {
		t.Fatal(err)
	}
	client := Test1Interface(Proxy{Channel: ch})
	server := Test1Service{}
	clientKey, err := server.Add(&Test1Impl{}, sh, nil)
	if err != nil {
		t.Fatal(err)
	}
	go Serve()

	t.Run("Basic", func(t *testing.T) {
		str := "Hello World!"
		r, err := client.Echo(&str)
		if err != nil {
			t.Fatal(err)
		}
		if r == nil {
			t.Fatal("unexpected nil result")
		}
		if *r != str {
			t.Fatalf("expected %s, got %s", str, *r)
		}
	})

	t.Run("Event", func(t *testing.T) {
		str := "Surprise!"
		done := make(chan struct{})
		// Spin up goroutine with waiting client.
		go func() {
			s, err := client.ExpectSurprise()
			if err != nil {
				t.Fatal(err)
			}
			if s != str {
				t.Fatalf("expected %s, got %s", str, s)
			}
			done <- struct{}{}
		}()
		// Spin up server goroutine which makes the call.
		go func() {
			pxy, ok := server.EventProxyFor(clientKey)
			if !ok {
				t.Fatalf("could not create proxy for key %d", clientKey)
			}
			if err := pxy.Surprise(str); err != nil {
				t.Fatal(err)
			}
		}()
		select {
		case <-done:
			return
		case <-time.After(5 * time.Second):
			t.Fatalf("test timed out")
		}
	})
}
