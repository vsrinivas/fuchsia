// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gonet_test

import (
	"fmt"
	"net"
	"runtime"
	"testing"
)

const loopbackAddr = "127.0.0.1:1234"
const msg = "hello"

func TestLoopbackStreamConnectRead(t *testing.T) {
	ln, err := net.Listen("tcp", loopbackAddr)
	if err != nil {
		t.Fatalf("net.Listen error: %v", err)
	}
	defer ln.Close()

	done := make(chan error, 1)
	go func() {
		done <- func() error {
			conn, err := net.Dial("tcp", loopbackAddr)
			if err != nil {
				return fmt.Errorf("net.Dial error: %v", err)
			}
			defer conn.Close()
			if _, err := conn.Write([]byte(msg)); err != nil {
				return fmt.Errorf("conn.Write error: %v", err)
			}
			return nil
		}()
	}()

	conn, err := ln.Accept()
	if err != nil {
		t.Fatalf("net.Accept error: %v", err)
	}
	defer conn.Close()

	if err := <-done; err != nil {
		t.Fatal(err)
	}

	b := make([]byte, 1024)
	n, err := conn.Read(b)
	if err != nil {
		t.Fatalf("conn.Read error: %v", err)
	}
	b = b[:n]
	if string(b) != msg {
		t.Fatalf("want = %v, got = %v", msg, string(b))
	}
}

func TestLoopbackDatagram(t *testing.T) {
	if runtime.GOOS == "fuchsia" {
		t.Skip("unimplemented; see https://fuchsia.googlesource.com/third_party/go/+/1064b67/src/net/udpsock_fuchsia.go")
	}

	conn, err := net.ListenPacket("udp", loopbackAddr)
	if err != nil {
		t.Fatalf("net.ListenPacket error: %v", err)
	}
	defer conn.Close()

	done := make(chan error, 1)
	go func() {
		done <- func() error {
			conn, err := net.Dial("udp", loopbackAddr)
			if err != nil {
				return fmt.Errorf("net.Dial error: %v", err)
			}
			defer conn.Close()

			if _, err = conn.Write([]byte(msg)); err != nil {
				return fmt.Errorf("conn.Write error: %v", err)
			}
			return nil
		}()
	}()

	if err := <-done; err != nil {
		t.Fatal(err)
	}

	b := make([]byte, 1024)
	n, _, err := conn.ReadFrom(b)
	if err != nil {
		t.Fatalf("conn.ReadFrom error: %v", err)
	}
	b = b[:n]
	if string(b) != msg {
		t.Fatalf("want = %v, got = %v", msg, string(b))
	}
}
