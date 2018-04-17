// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

import (
	"syscall/zx"
	"testing"
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
	bindings := BindingSet{}
	stub := &Test1Stub{Impl: &Test1Impl{}}
	if _, err := bindings.Add(stub, sh, nil); err != nil {
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
			t.Fatal("expected %s, got %s", str, *r)
		}
	})
}
